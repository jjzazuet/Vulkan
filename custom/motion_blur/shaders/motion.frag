#version 450

layout(set = 0, binding = 0) uniform sampler2D colorTex;
layout(set = 0, binding = 1) uniform sampler2D velocityTex;
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform MotionParams {
  float shutterAngle;
  float maxBlurPixels;
} p;

const int TAPS = 8;
const float FULL_TURN_DEGREES = 360.0;
const float MIN_BLUR_PIXELS = 0.5;

void main() {
  vec4 current = texture(colorTex, vUv);
  if (p.shutterAngle <= 0.0 || p.maxBlurPixels <= 0.0) {
    outColor = current;
    return;
  }

  vec2 velocityUv = texture(velocityTex, vUv).xy;
  vec2 imageSize = vec2(textureSize(colorTex, 0));
  vec2 velocityPx = velocityUv * imageSize * (p.shutterAngle / FULL_TURN_DEGREES);
  float lengthPx = length(velocityPx);
  if (lengthPx < MIN_BLUR_PIXELS) {
    outColor = current;
    return;
  }

  vec2 blurUv = velocityPx * min(1.0, p.maxBlurPixels / lengthPx) / imageSize;
  vec3 sum = current.rgb;
  for (int i = 1; i <= TAPS; i++) {
    vec2 offset = blurUv * (float(i) / float(2 * TAPS));
    sum += texture(colorTex, vUv + offset).rgb;
    sum += texture(colorTex, vUv - offset).rgb;
  }
  outColor = vec4(sum / float(1 + 2 * TAPS), 1.0);
}
