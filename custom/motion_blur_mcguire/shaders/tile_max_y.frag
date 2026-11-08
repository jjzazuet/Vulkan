#version 450

layout(set = 0, binding = 0) uniform sampler2D tileXTex;
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outTile;

#include "mcguire_common.glsl"

void main() {
  ivec2 imageSize = textureSize(tileXTex, 0);
  int k = int(p.tileSize);
  ivec2 tileCoord = ivec2(gl_FragCoord.xy);
  int y0 = tileCoord.y * k;

  vec2 maxVelocity = vec2(0.0);
  float maxLengthSq = 0.0;
  for (int i = 0; i < k; i++) {
    int y = y0 + i;
    if (y >= imageSize.y) {
      break;
    }
    vec2 velocity = decodeVelocity(texelFetch(tileXTex, ivec2(tileCoord.x, y), 0).rg);
    float lengthSq = dot(velocity, velocity);
    if (lengthSq > maxLengthSq) {
      maxVelocity = velocity;
      maxLengthSq = lengthSq;
    }
  }

  outTile = vec4(encodeVelocity(maxVelocity), 0.0, 1.0);
}
