#version 330 core
in  vec3 FragPos;
in  vec3 Normal;
in  vec2 TexCoord;

uniform vec3  objectColor;
uniform vec3  viewPos;
uniform int   isEmissive;
uniform int   isBackground;
uniform int   matType;      // 0=normal 1=grass 2=cobble 3=water 4=dirt 5=mixed 6=proc 7=image
uniform sampler2D tex0;

uniform float dayFactor;
uniform int   isRaining;
uniform vec3  skyColor;

uniform vec3  sunDir;
uniform vec3  sunColor;
uniform float sunIntensity;

uniform vec3  moonDir;
uniform vec3  moonColor;
uniform float fogDensity;

uniform vec3  firePos;
uniform vec3  fireColor;
uniform float fireIntensity;

uniform vec3  lampPos[12];
uniform float lampIntensity;  // ← এই লাইন যোগ করো (numLamps এর পরে)
uniform int   numLamps;
uniform float time;
uniform float thunderEffect; // 0 to 1 brightness boost

out vec4 FragColor;

// ── Hash / Noise Utilities ─────────────────────────────────────────────────
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}
vec2 hash2(vec2 p) {
    return fract(sin(vec2(dot(p, vec2(127.1, 311.7)),
                          dot(p, vec2(269.5, 183.3)))) * 43758.5453);
}

// Smooth value noise (bilinear interpolation)
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i),           hash(i + vec2(1,0)), f.x),
               mix(hash(i+vec2(0,1)), hash(i + vec2(1,1)), f.x), f.y);
}

// 4-octave fractal smooth noise – no more blocky patches
float fBm(vec2 p) {
    float v = 0.0, amp = 0.5, freq = 1.0;
    for (int i = 0; i < 4; i++) {
        v    += amp * vnoise(p * freq);
        freq *= 2.13;
        amp  *= 0.48;
    }
    return v;
}

// ── Voronoi for natural irregular cobblestone ──────────────────────────────
// Returns (edge_dist, per-cell hash value)
vec2 voronoi(vec2 uv) {
    vec2 i = floor(uv), f = fract(uv);
    float minDist  = 8.0;
    vec2  minCell  = vec2(0.0);
    vec2  minPoint = vec2(0.0);

    // Find nearest feature point
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 cell  = vec2(float(x), float(y));
            vec2 rnd   = hash2(i + cell);
            // Scatter feature points pseudo-randomly within each cell
            vec2 point = cell + 0.5 + 0.42 * sin(6.2831 * rnd);
            float d = length(point - f);
            if (d < minDist) {
                minDist  = d;
                minPoint = point;
                minCell  = i + cell;
            }
        }
    }

    // Second pass: find distance to nearest cell edge
    float edgeDist = 8.0;
    for (int y = -2; y <= 2; y++) {
        for (int x = -2; x <= 2; x++) {
            vec2 cell  = vec2(float(x), float(y));
            vec2 rnd   = hash2(i + cell);
            vec2 point = cell + 0.5 + 0.42 * sin(6.2831 * rnd);
            if (length(point - minPoint) > 0.001) {
                // Perpendicular bisector distance
                vec2 mid     = (minPoint + point) * 0.5;
                vec2 edgeDir = normalize(point - minPoint);
                float d      = dot(mid - f, edgeDir);
                edgeDist     = min(edgeDist, d);
            }
        }
    }

    return vec2(edgeDist, hash(minCell));
}

// ── Base Colour ────────────────────────────────────────────────────────────
vec3 getBaseColor()
{
    // ── Grass ──────────────────────────────────────────────────────────────
    if (matType == 1)
{
    vec2 uv = FragPos.xz;

    // Procedural variation (আগের মতোই)
    float large  = fBm(uv * 0.07);
    float medium = fBm(uv * 0.30);
    float fine   = fBm(uv * 1.10);
    float micro  = vnoise(uv * 4.5);
    float combined = large * 0.42 + medium * 0.30 + fine * 0.20 + micro * 0.08;

    // ── Triplanar texture sample (stretching ছাড়া) ──
    vec3 blend = abs(Normal);
    blend = max(blend, 0.00001);
    blend /= (blend.x + blend.y + blend.z);

    float texScale = 0.15; // বড় → টাইল ছোট, ছোট → বড় প্যাচ
    vec3 tx = texture(tex0, FragPos.yz * texScale).rgb;
    vec3 ty = texture(tex0, FragPos.xz * texScale).rgb;
    vec3 tz = texture(tex0, FragPos.xy * texScale).rgb;
    vec3 texColor = tx * blend.x + ty * blend.y + tz * blend.z;

    // Procedural variation দিয়ে texture এর উপর রঙ বৈচিত্র্য আনো
    float darken = 0.72 + combined * 0.55;
    vec3 g = texColor * darken;

    // ছায়া এলাকায় একটু সবুজ ঠান্ডা করো
    g = mix(g, g * vec3(0.85, 1.0, 0.80),
            smoothstep(0.55, 0.75, combined) * 0.35);

    if (isRaining == 1) g *= 0.72;
    return g;
}

    // ── Cobblestone – Voronoi irregular stones ─────────────────────────────
    if (matType == 2)
    {
        // ~3.5 tiles per world unit → realistic ~28 cm stones
        vec2 uv = FragPos.xz * 3.5;

        vec2  voro     = voronoi(uv);
        float edgeDist = voro.x;
        float cellHash = voro.y;

        // Soft mortar gap using smoothstep (no harsh step)
        float gapW    = 0.10;
        float inMortar = 1.0 - smoothstep(gapW * 0.3, gapW, edgeDist);

        // Per-stone brightness & tint variation (granite range 0.28 – 0.52)
        float bri  = 0.28 + cellHash * 0.24;
        float warm = fract(cellHash * 17.3);   // warm vs cool offset
        float aged = fract(cellHash *  5.7);   // age / weathering

        vec3 stoneBase = vec3(
            bri + warm * 0.06 - 0.02,
            bri + 0.01,
            bri - warm * 0.05 + 0.02
        );

        // Surface micro-texture (small chips / grain)
        float grain = vnoise(FragPos.xz * 14.0) * 0.06 - 0.03;
        stoneBase  += grain;

        // Moss in crevices and on damp/dark stones
        float mossNoise  = fBm(FragPos.xz * 0.9 + vec2(3.3, 7.1));
        float mossAmount = smoothstep(0.55, 0.82, mossNoise)
                         * smoothstep(0.55, 0.30, bri)  // more moss on darker stones
                         * 0.45;
        vec3  mossColor  = vec3(0.08, 0.17, 0.05);
        stoneBase = mix(stoneBase, mossColor, mossAmount);

        // Mortar: very dark with slight green tinge (organic grime)
        vec3 mortarColor = vec3(0.055, 0.085, 0.05);

        vec3 finalColor = mix(stoneBase, mortarColor, inMortar);

        // Wet cobblestone: saturated + darker
        if (isRaining == 1) {
            finalColor      *= 0.70;
            vec3 wetSpecular = finalColor * 1.5;
            finalColor       = mix(finalColor, wetSpecular, (1.0 - inMortar) * 0.25);
        }

        return finalColor;
    }

    // ── Water ──────────────────────────────────────────────────────────────
    if (matType == 3)
    {
        float n    = hash(floor(FragPos.xz * 6.0));
        float wave = sin(FragPos.x * 2.1 + time * 1.8) * 0.06
                   + sin(FragPos.z * 1.7 + time * 2.2) * 0.04;
        vec3 wDay   = mix(vec3(0.06,0.32,0.62), vec3(0.12,0.46,0.76), clamp(n+wave,0.0,1.0));
        vec3 wNight = mix(vec3(0.01,0.05,0.18), vec3(0.02,0.10,0.28), clamp(n+wave,0.0,1.0));
        return mix(wNight, wDay, dayFactor);
    }

    // ── Dirt / Road ────────────────────────────────────────────────────────
    if (matType == 4)
    {
        float n  = vnoise(FragPos.xz * 3.5);
        float n2 = vnoise(FragPos.xz * 8.8 + vec2(5.3, 1.7));
        vec3 base = mix(vec3(0.34,0.23,0.10), vec3(0.44,0.31,0.16), n);
        base      = mix(base, base * (0.88 + 0.22 * n2), 0.4);
        float rx  = abs(FragPos.x);
        float rut = smoothstep(0.5, 1.2, rx) * (1.0 - smoothstep(1.2, 2.8, rx));
        base     *= (1.0 - 0.10 * rut);
        if (isRaining == 1) base *= 0.68;
        return base;
    }

    // ── Mixed Pattern (Checkerboard) ─────────────────────────────────────
    if (matType == 5)
    {
        float scale = 4.0;
        vec2 grid = floor(TexCoord * scale);
        float pattern = mod(grid.x + grid.y, 2.0);
        vec3 col1 = vec3(0.1, 0.1, 0.1);
        vec3 col2 = vec3(0.9, 0.9, 0.9);
        return mix(col1, col2, pattern) * objectColor;
    }

    // ── Procedural Generation (Abstract Wood / Marble) ─────────────────────
    if (matType == 6)
    {
        float n = fBm(FragPos.xy * 2.0 + FragPos.zz * 1.5);
        float dist = length(FragPos.xz) * 1.5 + n * 0.8;
        float ring = fract(dist * 3.0);
        ring = smoothstep(0.0, 0.2, ring) - smoothstep(0.8, 1.0, ring);
        
        vec3 darkWood = vec3(0.25, 0.10, 0.05);
        vec3 lightWood = vec3(0.70, 0.45, 0.15);
        return mix(darkWood, lightWood, ring) * objectColor;
    }

    // ── Digitized Image (Regular UVs) ──────────────────────────────────────
    if (matType == 7)
    {
        vec3 texCol = texture(tex0, TexCoord).rgb;
        return texCol * objectColor;
    }

    // ── Triplanar Mapped Image (No stretching on boxes) ────────────────────
    if (matType == 8)
    {
        // Compute blending weights based on normal
        vec3 blend = abs(Normal);
        blend = max(blend, 0.00001); // Prevent division by zero
        blend /= (blend.x + blend.y + blend.z);
        
        float scale = 0.5; // Scale for tiling
        vec3 xaxis = texture(tex0, FragPos.yz * scale).rgb;
        vec3 yaxis = texture(tex0, FragPos.xz * scale).rgb;
        vec3 zaxis = texture(tex0, FragPos.xy * scale).rgb;
        
        vec3 texCol = xaxis * blend.x + yaxis * blend.y + zaxis * blend.z;
        return texCol * objectColor;
    }

    // ── Default solid object ────────────────────────────────────────────────
    float surf = vnoise(FragPos.xz * 5.5 + FragPos.y * 3.3);
    return objectColor * (0.92 + 0.08 * surf);
}

void main()
{
    vec3 color = getBaseColor();
    vec3 result;
    if (isEmissive == 1)
    {
        result = (matType == 7 || matType == 8) ? color : objectColor;
    }
    else
    {
        vec3 norm  = normalize(Normal);
        vec3 vDir  = normalize(viewPos - FragPos);

    // Cobblestone: subtle normal perturbation → gives 3D depth illusion
    if (matType == 2)
    {
        float nx = vnoise(FragPos.xz * 9.0 + vec2(17.3, 0.0)) - 0.5;
        float nz = vnoise(FragPos.xz * 9.0 + vec2(0.0, 31.7)) - 0.5;
        norm = normalize(norm + vec3(nx, 0.0, nz) * 0.18);
    }

    // Water ripple normal
    if (matType == 3)
    {
        float wx = sin(FragPos.x * 2.1 + time * 1.8) * 0.12
                 + sin(FragPos.z * 1.7 + time * 2.2) * 0.08;
        norm = normalize(norm + vec3(wx, 0.0, wx * 0.5));
    }

    // ── Ambient ────────────────────────────────────────────────────────────
    vec3 ambDay   = 0.28 * vec3(0.90, 0.92, 1.00);
    vec3 ambNight = 0.12 * vec3(0.20, 0.30, 0.65);
    vec3 ambient  = mix(ambNight, ambDay, dayFactor);
    if (isRaining == 1) ambient *= 0.44;

    // ── Sun ────────────────────────────────────────────────────────────────
    float sDiff  = max(dot(norm, normalize(sunDir)), 0.0);
    vec3  sunL   = sDiff * sunColor * sunIntensity * dayFactor;
    if (isRaining == 1) sunL *= 0.30;
    vec3  sunRef = reflect(-normalize(sunDir), norm);
    float sSpec  = pow(max(dot(vDir, sunRef), 0.0), 22.0);
    // Grass is matte (low specular), cobblestone is slightly shiny
    float specMul = (matType == 1) ? 0.015 : 0.12;
    vec3  sunSpec = specMul * sSpec * sunColor * dayFactor;
    if (isRaining == 1) sunSpec *= 0.4;
    if (isRaining == 1 && matType == 2) sunSpec *= 5.0; // wet cobble gleams

    // ── Moon ───────────────────────────────────────────────────────────────
    float mDiff = max(dot(norm, normalize(moonDir)), 0.0) * 0.55;
    vec3  moonL = mDiff * moonColor * (1.0 - dayFactor);
    float rim   = pow(1.0 - max(dot(norm, vDir), 0.0), 3.0);
    vec3  rimL  = rim * moonColor * 0.18 * (1.0 - dayFactor);

    // ── Campfire ───────────────────────────────────────────────────────────
    vec3  tF    = firePos - FragPos;
    float fDist = length(tF);
    float fAtt  = 1.0 / (0.25 + 0.07 * fDist + 0.035 * fDist * fDist);
    float fDiff = max(dot(norm, normalize(tF)), 0.0);
    vec3  fireL = fDiff * fireColor * fireIntensity * fAtt;
    vec3  fRef  = reflect(-normalize(tF), norm);
    float fSpec = pow(max(dot(vDir, fRef), 0.0), 24.0);
    vec3  fireS = 0.22 * fSpec * fireColor * fireIntensity * fAtt;

    // ── Lamps ──────────────────────────────────────────────────────────────
    vec3 lampL    = vec3(0.0);
    vec3 lampSpec = vec3(0.0);
    vec3 lampC    = vec3(1.0, 0.86, 0.48);
    for (int i = 0; i < numLamps && i < 12; i++)
    {
        vec3  tL   = lampPos[i] - FragPos;
        float lD   = length(tL);
        float lAtt = 1.0 / (0.45 + 0.12 * lD + 0.055 * lD * lD);
        float lDif = max(dot(norm, normalize(tL)), 0.0);
        lampL     += lDif * lampC * lampIntensity * lAtt;
        vec3  lRef = reflect(-normalize(tL), norm);
        float lSp  = pow(max(dot(vDir, lRef), 0.0), 30.0);
        float gm   = (isRaining == 1 && matType == 2) ? 8.0 : 0.5;
        lampSpec  += lSp * lampC * lampIntensity * lAtt * gm;
    }

    // ── AO hint ────────────────────────────────────────────────────────────
    float aoHint = 0.72 + 0.28 * clamp(norm.y, 0.0, 1.0);

        result = ((ambient + sunL + moonL) * color) * aoHint
                + sunSpec + rimL
                + fireL * color + fireS
                + lampL * color + lampSpec;

        // ── Subtle desaturation (less cartoonish) ──────────────────────────────
        float lum = dot(result, vec3(0.299, 0.587, 0.114));
        result     = mix(result, vec3(lum), 0.10);
    }

    // ── Height-biased fog ──────────────────────────────────────────────────
    float fogDens = isRaining == 1 ? fogDensity * 1.8 : fogDensity; 
    if (isBackground == 1) {
        fogDens = fogDens * 0.45; // Increased from 0.15 to 0.45 so fog visible on horizon
    }
    
    float fd      = length(FragPos - viewPos);
    float hf      = 1.0 + 0.35 * clamp(1.0 - FragPos.y * 0.45, 0.0, 1.0);
    float fog     = exp(-fd * fogDens * hf);
    result        = mix(skyColor, result, clamp(fog, 0.0, 1.0));

    // ── Thunder Flash ───────────────────────────────────────────────────────
    result += vec3(thunderEffect * 0.45, thunderEffect * 0.48, thunderEffect * 0.52);

    // Get alpha if it's a texture, else 1.0
    float alpha = 1.0;
    if (matType == 7 || matType == 8) {
        alpha = texture(tex0, TexCoord).a;
    }
    
    FragColor = vec4(result, alpha);
}
