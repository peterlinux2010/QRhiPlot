#version 440

layout(location = 0) in vec2 position;
layout(location = 1) in vec4 color;
layout(location = 0) out vec4 vColor;

layout(std140, binding = 0) uniform buf {
    vec4 axisRange; // xMin, xMax, yMin, yMax
    vec4 plotRect; // left, bottom, right, top in pre-flip NDC
    float y_dir;
};

void main()
{
    float xRange = max(axisRange.y - axisRange.x, 0.000001);
    float yRange = max(axisRange.w - axisRange.z, 0.000001);

    float normalizedX = (position.x - axisRange.x) / xRange;
    float normalizedY = (position.y - axisRange.z) / yRange;
    float ndcX = mix(plotRect.x, plotRect.z, normalizedX);
    float ndcY = mix(plotRect.y, plotRect.w, normalizedY);

    gl_Position = vec4(ndcX, ndcY * y_dir, 0.0, 1.0);
    vColor = color;
}
