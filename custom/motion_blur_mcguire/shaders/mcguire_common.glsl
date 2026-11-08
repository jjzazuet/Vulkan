#ifndef MCGUIRE_COMMON_GLSL
#define MCGUIRE_COMMON_GLSL

// Shared by the four fragment passes of the McGuire reconstruction chain.
// Velocity is stored in the tile/neighbor buffers as `velocityPx / maxBlurPixels`
// encoded into R8G8_UNORM with `encoded = value * 0.5 + 0.5`.

layout(push_constant) uniform MotionMcGuireParams {
  float shutterAngle;
  float maxBlurPixels;
  float tileSize;
  float sampleTaps;
} p;

const float FULL_TURN_DEGREES = 360.0;
const float MIN_BLUR_PIXELS = 0.5;
const float EPS = 1e-6;

vec2 encodeVelocity(vec2 normalized) {
  return normalized * 0.5 + 0.5;
}

vec2 decodeVelocity(vec2 encoded) {
  return encoded * 2.0 - 1.0;
}

// Pixel-space velocity clamped to the tile radius, matching the reconstruction
// filter's maximum displacement.
vec2 clampVelocityPx(vec2 velocityUv, vec2 imageSize) {
  vec2 px = velocityUv * imageSize * (p.shutterAngle / FULL_TURN_DEGREES);
  float len = length(px);
  float clamped = clamp(len, MIN_BLUR_PIXELS, min(p.tileSize, p.maxBlurPixels));
  return px * clamped / (len + EPS);
}

#endif
