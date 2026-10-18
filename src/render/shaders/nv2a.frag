#version 450
// The NV2A pixel pipeline: texture stages by SHADER_STAGE_PROGRAM mode, then
// the register combiners (up to eight general stages and the final
// combiner), then the alpha test. A line-by-line port of the executor's CPU
// path (xboxrecomp nv2a_combiner.c nv2a_rc_eval, nv2a_pb_exec.c
// rc_stage_fetch), which follows xemu's psh.c; keep the two in step.
// Batches without combiner state (flags bit 1 clear) draw stage-0 texel *
// diffuse, as milestone 2 did.

layout(location = 0) in vec4 v_diffuse;
layout(location = 1) in vec2 v_uv;
layout(location = 2) in vec4 v_tex0;
layout(location = 3) in vec4 v_tex1;
layout(location = 4) in vec4 v_tex2;
layout(location = 5) in vec4 v_tex3;
layout(location = 6) in vec4 v_specular;
layout(location = 7) in float v_fog;

layout(set = 0, binding = 0) uniform sampler2D tex0;
layout(set = 0, binding = 1) uniform sampler2D tex1;
layout(set = 0, binding = 2) uniform sampler2D tex2;
layout(set = 0, binding = 3) uniform sampler2D tex3;
// The same stages when their texture is a cube map (rc.misc.w bit per
// stage); the other binding of each pair holds a 1x1 white dummy.
layout(set = 0, binding = 5) uniform samplerCube cube0;
layout(set = 0, binding = 6) uniform samplerCube cube1;
layout(set = 0, binding = 7) uniform samplerCube cube2;
layout(set = 0, binding = 8) uniform samplerCube cube3;

// Combiner state for one batch; layout matches VkbCombiners in vk_backend.c.
layout(std140, set = 0, binding = 4) uniform RC {
    uvec4 cw[8];          // color_icw, alpha_icw, color_ocw, alpha_ocw
    vec4  c0[8];          // FACTOR0 per stage (already resolved for COMBINERCOUNT)
    vec4  c1[8];          // FACTOR1 per stage
    uvec4 fin;            // final0, final1, control, stage_program
    vec4  fc0, fc1;       // SPECULAR_FOG_FACTOR 0/1
    vec4  fog_color;      // rgb; .a unused
    vec4  tex_scale[4];   // xy: multiplies s/q, t/q into [0,1] (1/size for linear)
    uvec4 misc;           // x: clip_plane_mode, y: dot_mapping,
                          // z: stage_input, w: bit per cube-map stage
    vec4  eye;            // SET_EYE_VECTOR (DOT_RFLCT_SPEC_CONST)
} rc;

layout(push_constant) uniform PC {
    vec2  surface_size;
    vec2  tex_size;
    float z_scale;
    uint  flags;          // bit 0 stage 0 textured, bit 1 combiners,
                          // 8-10 alpha func, 16-23 ref, 31 alpha test on
} pc;

layout(location = 0) out vec4 o_color;

// NV2A alpha test: GL compare functions 0x200 (NEVER) .. 0x207 (ALWAYS),
// passed as 0..7, reference 0..255.
bool alpha_pass(float a)
{
    uint func = (pc.flags >> 8) & 7u;
    float ref = float((pc.flags >> 16) & 255u) / 255.0;
    // Compare on the 8-bit grid the hardware uses, not on floats.
    float av = floor(a * 255.0 + 0.5) / 255.0;
    switch (func) {
    case 0u: return false;
    case 1u: return av <  ref;
    case 2u: return av == ref;
    case 3u: return av <= ref;
    case 4u: return av >  ref;
    case 5u: return av != ref;
    case 6u: return av >= ref;
    default: return true;
    }
}

// ---- texture stages (rc_stage_fetch) ----------------------------------

vec4 stage_sample(int st, vec2 uv)
{
    // Samplers are indexed by constant only, as GLSL 450 requires.
    if (st == 0) return texture(tex0, uv);
    if (st == 1) return texture(tex1, uv);
    if (st == 2) return texture(tex2, uv);
    return texture(tex3, uv);
}

// A cube-map lookup by direction; a texture that is not a cube map reads
// (x, y) as 2D (rc_cube_fetch). Vulkan's face selection and orientation are
// the GL rules the CPU path implements by hand.
vec4 stage_cube(int st, vec3 d)
{
    if (((rc.misc.w >> st) & 1u) == 0u)
        return stage_sample(st, d.xy * rc.tex_scale[st].xy);
    if (d == vec3(0.0))
        d = vec3(1.0, 0.0, 0.0);
    if (st == 0) return texture(cube0, d);
    if (st == 1) return texture(cube1, d);
    if (st == 2) return texture(cube2, d);
    return texture(cube3, d);
}

// PS_DOTMAPPING (rc_dotmap): an input texel component as a vector component.
float dotmap(uint m, float x)
{
    float b = x * 255.0;
    switch (m & 7u) {
    case 1u: return (b - 128.0) / 127.0;
    case 2u: return b >= 128.0 ? (b - 255.5) / 127.5 : (b + 0.5) / 127.5;
    case 3u: return b >= 128.0 ? (b - 256.0) / 127.0 : b / 127.0;
    default: return x;
    }
}

// What the dot-product modes carry from stage to stage (RcDotState).
float g_dot[4];
float g_eye_w[4];

// dot(c.xyz, dotmap(t[input])) for stage st (1..3) (rc_stage_dot).
float stage_dot(int st, vec4 c, vec4 t[4])
{
    uint inp = st == 1 ? 0u
             : st == 2 ? (rc.misc.z >> 16) & 3u
                       : (rc.misc.z >> 20) & 3u;
    uint m = (rc.misc.y >> (4 * (st - 1))) & 15u;
    vec4 x = t[inp];
    return c.x * dotmap(m, x.x) + c.y * dotmap(m, x.y) + c.z * dotmap(m, x.z);
}

// Texture stage st with coordinates c; t[] holds the stages before it.
vec4 stage_fetch(int st, vec4 c, vec4 t[4])
{
    uint mode = (rc.fin.w >> (st * 5)) & 31u;
    float q = c.w != 0.0 ? c.w : 1.0;
    g_eye_w[st] = c.w;
    switch (mode) {
    case 0u:                                  // NONE
        return vec4(0.0, 0.0, 0.0, 1.0);
    case 1u: case 2u:                         // PROJECT2D / 3D
        return stage_sample(st, c.xy / q * rc.tex_scale[st].xy);
    case 3u:                                  // CUBEMAP
        return stage_cube(st, c.xyz);
    case 4u:                                  // PASSTHRU
        return clamp(c, 0.0, 1.0);
    case 5u: {                                // CLIPPLANE
        for (int j = 0; j < 4; j++) {
            bool ge = ((rc.misc.x >> (st * 4 + j)) & 1u) != 0u;
            if (ge ? c[j] >= 0.0 : c[j] < 0.0)
                discard;
        }
        return vec4(0.0);
    }
    // The dot-product modes: stages 1-3 take dot(texcoord.xyz, input texel);
    // the last stage of the chain looks a texture up with the vector.
    case 0x09u: case 0x0Du:                   // DOT_ST, DOT_STR_3D (as 2D)
        if (st < 1) break;
        g_dot[st] = stage_dot(st, c, t);
        return stage_sample(st, vec2(g_dot[1], g_dot[2]) * rc.tex_scale[st].xy);
    case 0x0Au: case 0x11u:                   // DOT_ZW, DOT_PRODUCT
        if (st < 1) break;
        g_dot[st] = stage_dot(st, c, t);
        return vec4(0.0);
    case 0x0Eu:                               // DOT_STR_CUBE
        if (st < 1) break;
        g_dot[st] = stage_dot(st, c, t);
        return stage_cube(st, vec3(g_dot[1], g_dot[2], g_dot[3]));
    case 0x0Cu: case 0x12u: {                 // DOT_RFLCT_SPEC(_CONST)
        if (st < 1) break;
        g_dot[st] = stage_dot(st, c, t);
        vec3 n = vec3(g_dot[1], g_dot[2], g_dot[3]);
        vec3 e = mode == 0x0Cu ? vec3(g_eye_w[1], g_eye_w[2], g_eye_w[3])
                               : rc.eye.xyz;
        float nn = dot(n, n);
        if (nn == 0.0) nn = 1.0;
        return stage_cube(st, 2.0 * n * dot(n, e) / nn - e);
    }
    default:
        break;
    }
    // Bump-env and DOT_RFLCT_DIFF read zero, as on the CPU path.
    return vec4(0.0);
}

// ---- register combiners (nv2a_rc_eval) --------------------------------

vec4 R[16];   // 0 zero, 1 c0, 2 c1, 3 fog, 4 v0, 5 v1, 8-11 t0-t3,
              // 12 r0, 13 r1, 14 v1+r0 sum, 15 E*F product

// PS_INPUTMAPPING (psh.c get_input_var).
float map_in(float x, uint b)
{
    float p = max(x, 0.0);
    switch ((b >> 5) & 7u) {
    case 0u: return p;
    case 1u: return 1.0 - clamp(x, 0.0, 1.0);
    case 2u: return 2.0 * p - 1.0;
    case 3u: return -2.0 * p + 1.0;
    case 4u: return p - 0.5;
    case 5u: return -p + 0.5;
    case 6u: return x;
    default: return -x;
    }
}

// PS_COMBINEROUTPUT scale/bias (psh.c get_output).
vec3 map_out3(vec3 x, uint m)
{
    switch (m) {
    case 0x08u: return x - 0.5;
    case 0x10u: return x * 2.0;
    case 0x18u: return (x - 0.5) * 2.0;
    case 0x20u: return x * 4.0;
    case 0x30u: return x * 0.5;
    default:    return x;
    }
}

float map_out1(float x, uint m)
{
    return map_out3(vec3(x), m).x;
}

vec3 read_rgb(uint b)
{
    vec4 r = R[b & 15u];
    vec3 v = (b & 0x10u) != 0u ? r.aaa : r.rgb;
    return vec3(map_in(v.x, b), map_in(v.y, b), map_in(v.z, b));
}

float read_alpha(uint b)
{
    vec4 r = R[b & 15u];
    return map_in((b & 0x10u) != 0u ? r.a : r.b, b);
}

vec4 combine(vec4 t[4])
{
    uint n = min(rc.fin.z & 255u, 8u), flags = rc.fin.z >> 8;

    for (int i = 0; i < 16; i++)
        R[i] = vec4(0.0);
    R[3] = vec4(rc.fog_color.rgb, v_fog);
    R[4] = v_diffuse;
    R[5] = v_specular;
    R[8] = t[0]; R[9] = t[1]; R[10] = t[2]; R[11] = t[3];
    // r0.a starts as texture 0's alpha, or 1 with stage 0 off.
    R[12].a = (rc.fin.w & 31u) != 0u ? t[0].a : 1.0;

    for (uint s = 0u; s < n; s++) {
        uint icw = rc.cw[s].x, aicw = rc.cw[s].y;
        uint ocw = rc.cw[s].z, aocw = rc.cw[s].w;
        uint fl = ocw >> 12, afl = aocw >> 12;
        R[1] = rc.c0[s];
        R[2] = rc.c1[s];
        bool mux_cd = (flags & 1u) != 0u ? R[12].a >= 0.5
                                         : (int(R[12].a * 255.0) & 1) != 0;

        // Both portions read before either writes.
        vec3 A = read_rgb(icw >> 24), B = read_rgb(icw >> 16);
        vec3 C = read_rgb(icw >> 8),  D = read_rgb(icw);
        float aA = read_alpha(aicw >> 24), aB = read_alpha(aicw >> 16);
        float aC = read_alpha(aicw >> 8),  aD = read_alpha(aicw);

        vec3 pab = (fl & 2u) != 0u ? vec3(dot(A, B)) : A * B;
        vec3 pcd = (fl & 1u) != 0u ? vec3(dot(C, D)) : C * D;
        vec3 sum = (fl & 4u) != 0u ? (mux_cd ? pcd : pab) : pab + pcd;
        vec3 ab = clamp(map_out3(pab, fl & 0x38u), -1.0, 1.0);
        vec3 cd = clamp(map_out3(pcd, fl & 0x38u), -1.0, 1.0);
        vec3 ms = clamp(map_out3(sum, fl & 0x38u), -1.0, 1.0);

        float apab = aA * aB, apcd = aC * aD;
        float asum = (afl & 4u) != 0u ? (mux_cd ? apcd : apab) : apab + apcd;
        float aab = clamp(map_out1(apab, afl & 0x38u), -1.0, 1.0);
        float acd = clamp(map_out1(apcd, afl & 0x38u), -1.0, 1.0);
        float ams = clamp(map_out1(asum, afl & 0x38u), -1.0, 1.0);

        // RGB destinations; blue-to-alpha also writes the destination's .a.
        uint d;
        if ((d = (ocw >> 4) & 15u) != 0u) {
            R[d].rgb = ab;
            if ((fl & 0x80u) != 0u) R[d].a = ab.b;
        }
        if ((d = ocw & 15u) != 0u) {
            R[d].rgb = cd;
            if ((fl & 0x40u) != 0u) R[d].a = cd.b;
        }
        if ((d = (ocw >> 8) & 15u) != 0u)
            R[d].rgb = ms;
        if ((d = (aocw >> 4) & 15u) != 0u) R[d].a = aab;
        if ((d = aocw & 15u) != 0u)        R[d].a = acd;
        if ((d = (aocw >> 8) & 15u) != 0u) R[d].a = ams;
        R[0] = vec4(0.0);
    }

    vec4 o;
    if (rc.fin.x != 0u || rc.fin.y != 0u) {
        uint f0 = rc.fin.x, f1 = rc.fin.y, ff = f1 & 255u;
        R[1] = rc.fc0;
        R[2] = rc.fc1;
        // V1R0 sum: optional complements (0x40 v1, 0x20 r0), clamp (0x80).
        vec3 sa = (ff & 0x40u) != 0u ? 1.0 - R[5].rgb : R[5].rgb;
        vec3 sb = (ff & 0x20u) != 0u ? 1.0 - R[12].rgb : R[12].rgb;
        R[14] = vec4((ff & 0x80u) != 0u ? clamp(sa + sb, 0.0, 1.0) : sa + sb, 0.0);
        vec3 E = read_rgb(f1 >> 24), F = read_rgb(f1 >> 16);
        R[15] = vec4(E * F, 0.0);
        vec3 A = read_rgb(f0 >> 24), B = read_rgb(f0 >> 16);
        vec3 C = read_rgb(f0 >> 8),  D = read_rgb(f0);
        o.rgb = D + C * (1.0 - A) + B * A;
        o.a = read_alpha(f1 >> 8);
    } else {
        o = R[12];
    }
    return clamp(o, 0.0, 1.0);
}

void main()
{
    vec4 c;
    if ((pc.flags & 2u) != 0u) {
        vec4 t[4] = vec4[4](vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0));
        for (int i = 0; i < 4; i++)
            g_dot[i] = g_eye_w[i] = 0.0;
        t[0] = stage_fetch(0, v_tex0, t);
        t[1] = stage_fetch(1, v_tex1, t);
        t[2] = stage_fetch(2, v_tex2, t);
        t[3] = stage_fetch(3, v_tex3, t);
        c = combine(t);
    } else {
        c = v_diffuse;
        if ((pc.flags & 1u) != 0u)
            c *= texture(tex0, v_uv);
    }
    if ((pc.flags & 0x80000000u) != 0u && !alpha_pass(c.a))
        discard;
    o_color = c;
}
