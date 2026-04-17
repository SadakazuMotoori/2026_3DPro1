#include "../inc_KdCommon.hlsli"
#include "inc_KdPostProcessShader.hlsli"

Texture2D g_depthTex : register(t0);
SamplerState g_ss : register(s0);

cbuffer cb : register(b0)
{
	float g_density;
	float g_weight;
	float g_decay;
	float g_strength;

	float3 g_color;
	float _blank;

	float g_sunDistance;
	float g_depthThreshold;
	float g_screenFadeStart;
	float g_screenFadeEnd;
};

float4 main(VSOutput In) : SV_Target0
{
	const int kSampleCount = 40;

	float dirLength = length(g_DL_Dir);
	if (dirLength <= 0.0001f)
	{
		return 0;
	}

	float3 sunWorldPos = g_CamPos - normalize(g_DL_Dir) * g_sunDistance;
	float4 sunViewPos = mul(float4(sunWorldPos, 1), g_mView);
	if (sunViewPos.z <= 0.0f)
	{
		return 0;
	}

	float4 sunClipPos = mul(sunViewPos, g_mProj);
	if (sunClipPos.w <= 0.0f)
	{
		return 0;
	}

	float2 sunNdc = sunClipPos.xy / sunClipPos.w;
	float2 sunUV = sunNdc * float2(0.5f, -0.5f) + 0.5f;

	float screenDistance = length(sunUV * 2.0f - 1.0f);
	float screenFade = 1.0f - smoothstep(g_screenFadeStart, g_screenFadeEnd, screenDistance);
	if (screenFade <= 0.0f)
	{
		return 0;
	}

	float currentDepth = g_depthTex.Sample(g_ss, In.UV).r;
	float currentOccluder = (currentDepth < g_depthThreshold) ? 1.0f : 0.0f;

	float2 rayStep = (sunUV - In.UV) * (g_density / kSampleCount);
	float2 sampleUV = In.UV;

	float shaft = 0.0f;
	float decay = 1.0f;

	[unroll]
	for (int i = 0; i < kSampleCount; ++i)
	{
		sampleUV += rayStep;

		float sampleDepth = g_depthTex.Sample(g_ss, sampleUV).r;
		float occluder = (sampleDepth < g_depthThreshold) ? 1.0f : 0.0f;

		shaft += occluder * decay * g_weight;
		decay *= g_decay;
	}

	shaft *= g_strength;
	shaft *= screenFade;
	shaft *= (1.0f - currentOccluder);

	return float4(g_color, shaft);
}
