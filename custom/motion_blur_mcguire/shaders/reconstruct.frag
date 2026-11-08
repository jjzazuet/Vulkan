#version 450

layout(set = 0, binding = 0) uniform sampler2D colorTex;
layout(set = 0, binding = 1) uniform sampler2D velocityTex;
layout(set = 0, binding = 2) uniform sampler2D depthTex;
layout(set = 0, binding = 3) uniform sampler2D neighborTex;
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

#include "mcguire_common.glsl"

const float NEAR_PLANE = 0.1;
const float FAR_PLANE = 100.0;
// Documented soft-depth extent in linearized view units, shared by C++ and Java.
const float SOFT_Z_EXTENT = 0.02;

float linearizeDepth(float depth) {
  float projX = -FAR_PLANE / (FAR_PLANE - NEAR_PLANE);
  float projY = -FAR_PLANE * NEAR_PLANE / (FAR_PLANE - NEAR_PLANE);
  return -projY / (depth + projX);
}

float softDepthCompare(float za, float zb) {
  return clamp(1.0 - (za - zb) / SOFT_Z_EXTENT, 0.0, 1.0);
}

float cone(float pxDistance, float velocity) {
  return clamp(1.0 - pxDistance / max(velocity, EPS), 0.0, 1.0);
}

float cylinder(float pxDistance, float velocity) {
  float v = max(velocity, EPS);
  return 1.0 - smoothstep(0.95 * v, 1.05 * v, pxDistance);
}

float rand(vec2 co) {
  return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

vec3 velocityConvertPx(vec2 velocityUv, vec2 imageSize) {
  vec2 px = velocityUv * imageSize * (p.shutterAngle / FULL_TURN_DEGREES);
  float len = length(px);
  float clamped = clamp(len, MIN_BLUR_PIXELS, min(p.tileSize, p.maxBlurPixels));
  return vec3(px * clamped / (len + EPS), clamped);
}

void main() {
  ivec2 imageSize = textureSize(colorTex, 0);
  ivec2 coord = ivec2(gl_FragCoord.xy);
  vec4 current = texelFetch(colorTex, coord, 0);
  if (p.shutterAngle <= 0.0 || p.maxBlurPixels <= 0.0) {
    outColor = current;
    return;
  }

  int k = int(p.tileSize);
  vec2 neighborVelocity = decodeVelocity(
    texelFetch(neighborTex, coord / k, 0).rg
  );
  vec2 blurPx = neighborVelocity * p.maxBlurPixels;
  if (length(blurPx) < MIN_BLUR_PIXELS) {
    outColor = current;
    return;
  }

  vec3 velocityX = velocityConvertPx(
    texelFetch(velocityTex, coord, 0).rg, vec2(imageSize)
  );
  float lengthX = velocityX.z;
  float weight = 1.0 / lengthX;
  vec4 sum = current * weight;

  int taps = int(p.sampleTaps);
  float jitter = rand(vec2(coord)) - 0.5;
  float halfTexel = 0.5 / float(imageSize.x);
  for (int i = 0; i < taps; i++) {
    if (i == taps / 2) {
      continue;
    }
    float t = mix(
      -1.0, 1.0, (float(i) + jitter + 1.0) / (float(taps) + 1.0)
    );
    vec2 offsetPx = blurPx * (t + halfTexel);
    ivec2 sampleCoord = clamp(
      ivec2(vec2(coord) + offsetPx), ivec2(0), imageSize - 1
    );
    float offsetLengthPx = length(offsetPx);

    float depthX = linearizeDepth(texelFetch(depthTex, coord, 0).r);
    float depthY = linearizeDepth(texelFetch(depthTex, sampleCoord, 0).r);
    float foreground = softDepthCompare(depthX, depthY);
    float background = softDepthCompare(depthY, depthX);

    vec3 velocityY = velocityConvertPx(
      texelFetch(velocityTex, sampleCoord, 0).rg, vec2(imageSize)
    );
    float lengthY = velocityY.z;
    float ay = foreground * cone(offsetLengthPx, lengthY)
      + background * cone(offsetLengthPx, lengthX)
      + cylinder(offsetLengthPx, lengthY)
        * cylinder(offsetLengthPx, lengthX) * 2.0;

    weight += ay;
    sum += ay * texelFetch(colorTex, sampleCoord, 0);
  }

  outColor = sum / max(weight, EPS);
}
