#version 330 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoord;

void main()
{
    vec4 worldPos  = model * vec4(position, 1.0);
    FragPos        = worldPos.xyz;
    mat3 normalMat = transpose(inverse(mat3(model)));
    Normal         = normalize(normalMat * normal);
    TexCoord       = texCoord;
    gl_Position    = projection * view * worldPos;
    gl_PointSize   = 3.0;
}
