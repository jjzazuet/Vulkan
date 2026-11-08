#version 450

layout(set = 0, binding = 0) uniform sampler2D velocityTex;
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outTile;

#include "mcguire_common.glsl"

void main() {
  ivec2 imageSize = textureSize(velocityTex, 0);
  int k = int(p.tileSize);
  ivec2 tileCoord = ivec2(gl_FragCoord.xy);
  int x0 = tileCoord.x * k;

  vec2 maxVelocity = vec2(0.0);
  float maxLengthSq = 0.0;
  for (int i = 0; i < k; i++) {
    int x = x0 + i;
    if (x >= imageSize.x) {
      break;
    }
    vec2 velocity = texelFetch(velocityTex, ivec2(x, tileCoord.y), 0).rg;
    float lengthSq = dot(velocity, velocity);
    if (lengthSq > maxLengthSq) {
      maxVelocity = velocity;
      maxLengthSq = lengthSq;
    }
  }

  vec2 px = clampVelocityPx(maxVelocity, vec2(imageSize));
  outTile = vec4(encodeVelocity(px / max(p.maxBlurPixels, EPS)), 0.0, 1.0);
}
