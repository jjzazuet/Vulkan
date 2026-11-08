#version 450

layout(set = 0, binding = 0) uniform sampler2D tileYTex;
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outNeighbor;

#include "mcguire_common.glsl"

void main() {
  ivec2 imageSize = textureSize(tileYTex, 0);
  ivec2 coord = ivec2(gl_FragCoord.xy);

  vec2 maxVelocity = vec2(0.0);
  float maxLengthSq = 0.0;
  for (int y = -1; y <= 1; y++) {
    for (int x = -1; x <= 1; x++) {
      ivec2 sampleCoord = clamp(
        coord + ivec2(x, y), ivec2(0), imageSize - 1
      );
      vec2 velocity = decodeVelocity(texelFetch(tileYTex, sampleCoord, 0).rg);
      float lengthSq = dot(velocity, velocity);
      if (lengthSq > maxLengthSq) {
        maxVelocity = velocity;
        maxLengthSq = lengthSq;
      }
    }
  }

  outNeighbor = vec4(encodeVelocity(maxVelocity), 0.0, 1.0);
}
