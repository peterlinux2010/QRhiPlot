#version 440

layout(location = 0) in vec4 vColor;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    vec4 axisRange;
    vec4 plotRect;
    float y_dir;
};

void main()
{
    fragColor = vColor;
}
