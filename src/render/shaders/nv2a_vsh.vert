#version 450
// The title's NV2A vertex program, run per vertex on the GPU: the executor
// hands over the untransformed vertices, the decoded program and its
// constants (Nv2aProgramBatch in nv2a_backend.h), and this interprets the
// program exactly as the executor's CPU interpreter does (nv2a_vsh_run in
// nv2a_vsh_interp.c) -- same fields, same op semantics, same defaults.
// Outputs match nv2a.vert, so nv2a.frag is shared.

layout(push_constant) uniform PC {
    vec2  surface_size;   // real (anti-aliased) pixels
    vec2  tex_size;       // texels of stage 0; (1,1) when untextured
    float z_scale;        // 1 / depth-buffer maximum (Z24: 1/16777215)
    uint  flags;          // see nv2a.frag
} pc;

// c[0..191]
layout(set = 0, binding = 9, std140) uniform Consts { vec4 c[192]; } K;
// nv2a_vsh_export layout, one uvec4 per instruction
layout(set = 0, binding = 10, std140) uniform Prog { uvec4 ins[136]; } P;
// Per batch: attribute formats (x: byte offset into D, y: stride, z: type,
// w: size; size 0 = read imm), the immediate values, and misc:
// misc[0] = aa scale xy, uv scale zw; misc[1] = fog param x, y, fog enable
// (as float), fog mode (as float); misc[2].x = program length.
layout(set = 0, binding = 11, std140) uniform Attrs {
    uvec4 fmt[16];
    vec4  imm[16];
    vec4  misc[3];
} A;
// The batch's vertex bytes (all attributes' spans, packed).
layout(set = 0, binding = 12, std430) readonly buffer VData { uint w[]; } D;

layout(location = 0) out vec4 v_diffuse;
layout(location = 1) out vec2 v_uv;
layout(location = 2) out vec4 v_tex0;
layout(location = 3) out vec4 v_tex1;
layout(location = 4) out vec4 v_tex2;
layout(location = 5) out vec4 v_tex3;
layout(location = 6) out vec4 v_specular;
layout(location = 7) out float v_fog;

uint ld32(uint at)
{
    uint i = at >> 2u, s = (at & 3u) * 8u;
    if (s == 0u)
        return D.w[i];
    return (D.w[i] >> s) | (D.w[i + 1u] << (32u - s));
}

int ld16s(uint at)
{
    uint v = ld32(at) & 0xFFFFu;
    return int(v << 16) >> 16;
}

vec4 fetch(int a, uint vi)
{
    uvec4 f = A.fmt[a];
    vec4 r = vec4(0.0, 0.0, 0.0, 1.0);
    uint n = min(f.w, 4u);

    if (f.w == 0u)
        return A.imm[a];
    uint at = f.x + vi * f.y;
    switch (f.z) {
    case 0u: {                                       // D3DCOLOR: B,G,R,A bytes
        uint v = ld32(at);
        r = vec4(float((v >> 16) & 255u), float((v >> 8) & 255u),
                 float(v & 255u), float(v >> 24)) / 255.0;
        break;
    }
    case 2u:                                         // float
        for (uint i = 0u; i < n; i++)
            r[i] = uintBitsToFloat(ld32(at + i * 4u));
        break;
    case 4u:                                         // ubyte, normalised
        for (uint i = 0u; i < n; i++)
            r[i] = float((ld32(at + i) & 255u)) / 255.0;
        break;
    case 1u:                                         // short, normalised
        for (uint i = 0u; i < n; i++)
            r[i] = float(ld16s(at + i * 2u)) / 32767.0;
        break;
    case 5u:                                         // short, as is
        for (uint i = 0u; i < n; i++)
            r[i] = float(ld16s(at + i * 2u));
        break;
    case 6u: {                                       // packed 11:11:10
        uint v = ld32(at);
        r.x = float(int(v << 21) >> 21) / 1023.0;
        r.y = float(int(v << 10) >> 21) / 1023.0;
        r.z = float(int(v) >> 22) / 511.0;
        break;
    }
    default:
        break;
    }
    return r;
}

vec4 V[16];
vec4 R[13];          // R0..R11, R12 = oPos
vec4 O[13];          // output registers (o[0] is oPos, kept in R[12])
int  a0 = 0;

vec4 read_src(uint s)
{
    uint mux = (s >> 1) & 3u, reg = (s >> 3) & 15u, idx = (s >> 16) & 255u;
    vec4 v;

    if (mux == 1u)
        v = reg <= 12u ? R[reg] : R[0];
    else if (mux == 2u)
        v = V[idx & 15u];
    else {
        int ci = int(idx);
        if (((s >> 7) & 1u) != 0u)
            ci += a0;
        if (ci < 0 || ci >= 192)
            ci = 0;
        v = K.c[ci];
    }
    v = vec4(v[(s >> 8) & 3u], v[(s >> 10) & 3u], v[(s >> 12) & 3u], v[(s >> 14) & 3u]);
    return (s & 1u) != 0u ? -v : v;
}

// Masks: x = bit 3 .. w = bit 0.
vec4 masked(vec4 dst, vec4 src, uint m)
{
    bvec4 b = bvec4((m & 8u) != 0u, (m & 4u) != 0u, (m & 2u) != 0u, (m & 1u) != 0u);
    return mix(dst, src, b);
}

vec4 run_mac(uint op, vec4 a, vec4 b, vec4 c)
{
    switch (op) {
    case 1u: return a;                                              // mov
    case 2u: return a * b;                                          // mul
    case 3u: return a + c;                                          // add
    case 4u: return a * b + c;                                      // mad
    case 5u: return vec4(dot(a.xyz, b.xyz));                        // dp3
    case 6u: return vec4(dot(a.xyz, b.xyz) + b.w);                  // dph
    case 7u: return vec4(dot(a, b));                                // dp4
    case 8u: return vec4(1.0, a.y * b.y, a.z, b.w);                 // dst
    case 9u: return vec4(a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y,
                         a.z < b.z ? a.z : b.z, a.w < b.w ? a.w : b.w);   // min
    case 10u: return vec4(a.x >= b.x ? a.x : b.x, a.y >= b.y ? a.y : b.y,
                          a.z >= b.z ? a.z : b.z, a.w >= b.w ? a.w : b.w); // max
    case 11u: return vec4(lessThan(a, b));                          // slt
    case 12u: return vec4(greaterThanEqual(a, b));                  // sge
    case 13u: a0 = int(floor(a.x + 0.001)); return a;               // arl
    default: return a;
    }
}

vec4 run_ilu(uint op, vec4 c)
{
    float x = c.x;
    switch (op) {
    case 1u: return c;                                              // mov
    case 2u: return vec4(x == 0.0 ? uintBitsToFloat(0x7F800000u) : 1.0 / x);   // rcp
    case 3u: {                                                      // rcc
        float f = 1.0 / x;
        if (abs(f) < 5.42101e-20) f = f < 0.0 ? -5.42101e-20 : 5.42101e-20;
        if (abs(f) > 1.884467e19) f = f < 0.0 ? -1.884467e19 : 1.884467e19;
        return vec4(f);
    }
    case 4u: return vec4(inversesqrt(abs(x)));                      // rsq
    case 5u: {                                                      // exp
        float fl = floor(x);
        return vec4(exp2(fl), x - fl, exp2(x), 1.0);
    }
    case 6u: {                                                      // log
        float ax = abs(x);
        if (ax == 0.0)
            return vec4(uintBitsToFloat(0xFF800000u), 1.0, uintBitsToFloat(0xFF800000u), 1.0);
        float e = floor(log2(ax));
        return vec4(e, ax / exp2(e), log2(ax), 1.0);
    }
    case 7u: {                                                      // lit
        float nl = max(c.x, 0.0), nh = max(c.y, 0.0);
        float p = clamp(c.w, -127.9961, 127.9961);
        return vec4(1.0, nl, c.x > 0.0 ? pow(nh, p) : 0.0, 1.0);
    }
    default: return c;
    }
}

float fog_factor(float d)
{
    float px = A.misc[1].x, py = A.misc[1].y, f;
    uint mode = uint(A.misc[1].w);

    if (A.misc[1].z == 0.0)
        return 1.0;
    if (mode == 0x800u || mode == 0x802u)                           // EXP
        f = px + exp2(d * py * 16.0) - 1.5;
    else if (mode == 0x801u || mode == 0x803u)                      // EXP2
        f = px + exp2(-d * d * py * py * 32.0) - 1.5;
    else                                                            // LINEAR
        f = px + d * py - 1.0;
    if (mode == 0x802u || mode == 0x803u || mode == 0x804u)
        f = abs(f);
    if (isnan(f))
        f = 1.0;
    return clamp(f, 0.0, 1.0);
}

void main()
{
    uint vi = uint(gl_VertexIndex), n = uint(A.misc[2].x);

    for (int a = 0; a < 16; a++)
        V[a] = fetch(a, vi);
    for (int i = 0; i < 13; i++) {
        R[i] = vec4(0.0);
        O[i] = vec4(0.0);
    }
    O[3].w = 1.0; O[4].w = 1.0;                       // colours default opaque
    O[9].w = 1.0; O[10].w = 1.0; O[11].w = 1.0; O[12].w = 1.0;

    for (uint s = 0u; s < n; s++) {
        uvec4 I = P.ins[s];
        uint w0 = I.x;
        uint mac = w0 & 15u, ilu = (w0 >> 4) & 7u;
        uint mac_mask = (w0 >> 7) & 15u, ilu_mask = (w0 >> 11) & 15u;
        uint tdst = (w0 >> 15) & 15u, omask = (w0 >> 19) & 15u;
        uint oaddr = I.y >> 24;
        vec4 a = vec4(0.0), b = vec4(0.0), c = vec4(0.0);
        vec4 mres = vec4(0.0), ires = vec4(0.0);

        if ((w0 & (1u << 25)) != 0u) a = read_src(I.y);
        if ((w0 & (1u << 26)) != 0u) b = read_src(I.z);
        if ((w0 & (1u << 27)) != 0u) c = read_src(I.w);
        if (mac != 0u)
            mres = run_mac(mac, a, b, c);
        if (ilu != 0u)
            ires = run_ilu(ilu, c);
        if (mac != 0u && mac != 13u && mac_mask != 0u && tdst <= 12u)
            R[tdst] = masked(R[tdst], mres, mac_mask);
        if (ilu != 0u && ilu_mask != 0u) {
            // Paired with a MAC op, the ILU can only write R1 (see nv2a_vsh_run).
            uint it = mac != 0u ? 1u : tdst;
            if (it <= 12u)
                R[it] = masked(R[it], ires, ilu_mask);
        }
        if (omask != 0u && ((w0 >> 24) & 1u) != 0u) {   // to o[]; c[] writes stay on the CPU
            vec4 src = ((w0 >> 23) & 1u) != 0u ? ires : mres;
            if (oaddr == 0u)
                R[12] = masked(R[12], src, omask);
            else if (oaddr < 13u)
                O[oaddr] = masked(O[oaddr], src, omask);
        }
    }

    vec4 pos = R[12];
    // oPos.w == +0: a program that never divided -- already surface units.
    if (pos.w == 0.0 && (floatBitsToUint(pos.w) & 0x80000000u) == 0u)
        pos.w = 1.0;
    pos.xy *= A.misc[0].xy;
    // Surface pixels (y down, as Vulkan's framebuffer), back to clip space
    // with the program's w, so the GPU clips at w > 0 and interpolates
    // perspective-correctly.
    vec2 ndc = pos.xy / pc.surface_size * 2.0 - 1.0;
    gl_Position = vec4(ndc * pos.w, pos.z * pc.z_scale * pos.w, pos.w);

    v_diffuse = clamp(O[3], 0.0, 1.0);
    v_specular = clamp(O[4], 0.0, 1.0);
    v_fog = fog_factor(O[5].x);
    v_tex0 = O[9];
    v_tex1 = O[10];
    v_tex2 = O[11];
    v_tex3 = O[12];
    v_uv = O[9].xy * A.misc[0].zw / pc.tex_size;
}
