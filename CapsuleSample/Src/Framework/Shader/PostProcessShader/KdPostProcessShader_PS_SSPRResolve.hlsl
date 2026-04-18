#include "inc_KdPostProcessShader.hlsli"

#define SSPR_MIRROR_MAX 16

Texture2D g_sceneTex : register(t0);
Texture2D<uint> g_projectionTex : register(t1);
Texture2D g_depthTex : register(t2);

SamplerState g_ss : register(s0);

cbuffer cbSSPR : register(b1)
{
	row_major float4x4 g_mView;
	row_major float4x4 g_mProj;
	row_major float4x4 g_mProjInv;
	row_major float4x4 g_mViewInv;

	float2 g_screenSize;
	float2 g_invScreenSize;
	int g_mirrorCount;
	float3 g_blank0;

	float4 g_planePosStrength[SSPR_MIRROR_MAX];
	float4 g_planeNormalBias[SSPR_MIRROR_MAX];
	float4 g_planeRightHalfWidth[SSPR_MIRROR_MAX];
	float4 g_planeUpHalfHeight[SSPR_MIRROR_MAX];
	float4 g_planeParams[SSPR_MIRROR_MAX];
};

float3 GetPlanePos(int mirrorIdx) { return g_planePosStrength[mirrorIdx].xyz; }
float GetReflectionStrength(int mirrorIdx) { return g_planePosStrength[mirrorIdx].w; }
float3 GetPlaneNormal(int mirrorIdx) { return g_planeNormalBias[mirrorIdx].xyz; }
float GetPlaneBias(int mirrorIdx) { return g_planeNormalBias[mirrorIdx].w; }
float3 GetPlaneRight(int mirrorIdx) { return g_planeRightHalfWidth[mirrorIdx].xyz; }
float GetHalfWidth(int mirrorIdx) { return g_planeRightHalfWidth[mirrorIdx].w; }
float3 GetPlaneUp(int mirrorIdx) { return g_planeUpHalfHeight[mirrorIdx].xyz; }
float GetHalfHeight(int mirrorIdx) { return g_planeUpHalfHeight[mirrorIdx].w; }
float GetRoughness(int mirrorIdx) { return saturate(g_planeParams[mirrorIdx].x); }

float3 ReconstructWorldPos(float2 uv, float depth)
{
	float2 projXY = (uv * 2.0f - 1.0f) * float2(1.0f, -1.0f);
	float4 viewPos = mul(float4(projXY, depth, 1.0f), g_mProjInv);
	viewPos.xyz /= viewPos.w;

	float4 worldPos = mul(float4(viewPos.xyz, 1.0f), g_mViewInv);
	return worldPos.xyz;
}

bool IsMirrorSurface(float3 worldPos, int mirrorIdx, out float planeDistance)
{
	float3 fromPlane = worldPos - GetPlanePos(mirrorIdx);
	planeDistance = abs(dot(fromPlane, GetPlaneNormal(mirrorIdx)));
	if (planeDistance > GetPlaneBias(mirrorIdx))
	{
		return false;
	}

	float rightDistance = abs(dot(fromPlane, GetPlaneRight(mirrorIdx)));
	float upDistance = abs(dot(fromPlane, GetPlaneUp(mirrorIdx)));

	return rightDistance <= GetHalfWidth(mirrorIdx) && upDistance <= GetHalfHeight(mirrorIdx);
}

int FindMirrorIndex(float3 worldPos)
{
	int foundMirror = -1;
	float bestDistance = 999999.0f;

	for (int mirrorIdx = 0; mirrorIdx < g_mirrorCount; ++mirrorIdx)
	{
		float planeDistance = 0.0f;
		if (!IsMirrorSurface(worldPos, mirrorIdx, planeDistance))
		{
			continue;
		}

		if (planeDistance < bestDistance)
		{
			bestDistance = planeDistance;
			foundMirror = mirrorIdx;
		}
	}

	return foundMirror;
}

bool DecodeProjection(uint packedValue, out float2 srcUV)
{
	if (packedValue == 0xFFFFFFFF)
	{
		srcUV = 0;
		return false;
	}

	uint srcX = packedValue & 0x0FFF;
	uint srcY = (packedValue >> 12) & 0x0FFF;
	srcUV = (float2(srcX, srcY) + 0.5f) * g_invScreenSize;
	return true;
}

float4 SampleReflectionColor(float2 srcUV, float roughness)
{
	float4 sharpColor = g_sceneTex.SampleLevel(g_ss, srcUV, 0);
	if (roughness <= 0.001f)
	{
		return sharpColor;
	}

	// roughness は 0.1 を超えたあたりから急に効かせ、1.0 では形だけ分かる程度まで崩す。
	float blurAmount = saturate((roughness - 0.1f) / 0.9f);
	blurAmount = pow(blurAmount, 0.5f);
	float2 blurStep = g_invScreenSize * (1.0f + blurAmount * 24.0f);
	float4 blurColor = 0;
	float totalWeight = 0;

	[unroll]
	for (int y = -1; y <= 1; ++y)
	{
		[unroll]
		for (int x = -1; x <= 1; ++x)
		{
			float weight = 1.0f / (1.0f + abs((float)x) + abs((float)y));
			float2 sampleUV = srcUV + float2(x, y) * blurStep;
			blurColor += g_sceneTex.SampleLevel(g_ss, sampleUV, 0) * weight;
			totalWeight += weight;
		}
	}

	blurColor /= totalWeight;
	return lerp(sharpColor, blurColor, blurAmount);
}

float4 main(VSOutput In) : SV_Target0
{
	float4 baseColor = g_sceneTex.Sample(g_ss, In.UV);

	float depth = g_depthTex.Sample(g_ss, In.UV).r;
	if (depth >= 1.0f)
	{
		return baseColor;
	}

	float3 worldPos = ReconstructWorldPos(In.UV, depth);
	int mirrorIdx = FindMirrorIndex(worldPos);
	if (mirrorIdx < 0)
	{
		return baseColor;
	}

	float reflectionStrength = GetReflectionStrength(mirrorIdx);
	float roughness = GetRoughness(mirrorIdx);
	float blurAmount = saturate((roughness - 0.1f) / 0.9f);
	blurAmount = pow(blurAmount, 0.5f);
	float maxSampleRadius = lerp(2.0f, 4.0f, blurAmount);
	float sigma = lerp(0.85f, 2.4f, blurAmount);

	uint2 pixelPos = uint2(In.Pos.xy);
	float4 reflectionColor = 0;
	float totalWeight = 0;

	// 投影先対応表には欠けが出やすいので、近傍を見ながら埋めて格子模様を抑える。
	for (int y = -4; y <= 4; ++y)
	{
		for (int x = -4; x <= 4; ++x)
		{
			float2 tapOffset = float2((float)x, (float)y);
			float sampleDistance = length(tapOffset);
			if (sampleDistance > maxSampleRadius)
			{
				continue;
			}

			int2 samplePos = int2(pixelPos) + int2(x, y);
			if (samplePos.x < 0 || samplePos.y < 0 || samplePos.x >= g_screenSize.x || samplePos.y >= g_screenSize.y)
			{
				continue;
			}

			float2 srcUV;
			if (!DecodeProjection(g_projectionTex.Load(int3(samplePos, 0)), srcUV))
			{
				continue;
			}

			float weight = exp(-(sampleDistance * sampleDistance) / (2.0f * sigma * sigma));
			reflectionColor += g_sceneTex.SampleLevel(g_ss, srcUV, 0) * weight;
			totalWeight += weight;
		}
	}

	if (totalWeight <= 0.0f)
	{
		return baseColor;
	}

	reflectionColor /= totalWeight;
	return lerp(baseColor, reflectionColor, reflectionStrength);
}
