#version 440

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;

layout(location = 0) out vec2 fragTexCoord;

layout(std140, binding = 0) uniform buf {
    float scrollOffset;   // Row scroll offset for circular buffer
    float binCount;       // Full tier bin count (e.g., 1024)
    float textureWidth;   // Texture width (e.g., 4096)
    float tierSpanHz;     // Full tier bandwidth in Hz
    float spanHz;         // Display span in Hz
    float padding[3];
};

void main() {
    gl_Position = vec4(position, 0.0, 1.0);
    fragTexCoord = vec2(texCoord.x, texCoord.y + scrollOffset);
}
