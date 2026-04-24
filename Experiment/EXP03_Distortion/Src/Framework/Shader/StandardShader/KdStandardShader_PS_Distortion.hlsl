#include "inc_KdStandardShader.hlsli"
#include "../inc_KdCommon.hlsli"

Texture2D g_tex : register(t0);

SamplerState g_ss : register(s0);

cbuffer cbDistortion : register(b4)
{
	// CPU 側で設定した「どの種類の歪みを描くか」。
	int g_distortionType;
	// 最終合成時に使う、背景のずらし量の強さ。
	float g_distortionStrength;
	// ノイズスクロールなどの進行時間。
	float g_distortionTime;
	float g_distortionBlank0;

	// 用途切り替え用の汎用パラメータ。
	// どの値をどの意味で使うかは、歪み種類ごとに決める。
	float4 g_distortionParam0;
	float4 g_distortionParam1;
};

// 板ポリの端まで歪ませると境界が不自然に見えやすいため、
// 端へ近づくほど影響を弱めて自然に消す。
float CalcEdgeFade(float2 uv)
{
	float2 fadeIn = smoothstep(0.0f, 0.15f, uv);
	float2 fadeOut = smoothstep(0.0f, 0.15f, 1.0f - uv);

	return fadeIn.x * fadeIn.y * fadeOut.x * fadeOut.y;
}

// 中心から一定距離だけ強くなる輪状マスクを作る。
// 衝撃波ではこの値を使って「輪の部分だけ歪む」状態を作る。
float CalcShockWaveMask(float2 centeredUV, float radius, float thickness)
{
	float dist = length(centeredUV);
	float safeThickness = max(thickness, 0.0001f);

	float ring = saturate(1.0f - abs(dist - radius) / safeThickness);

	return ring * ring;
}

float4 main(VSOutputNoLighting In) : SV_Target0
{
	float2 uv = In.UV;
	float edgeFade = CalcEdgeFade(uv);

	// 頂点カラーとマテリアルカラーの Alpha を合わせ、
	// 歪みの基本的な有効範囲を作る。
	float mask = edgeFade * In.Color.a * g_BaseColor.a;
	float2 offset = 0.0f;

	// 熱気・蜃気楼表現。
	if (g_distortionType == 1)
	{
		// ノイズを時間で流し、
		// 0 ～ 1 の値を -1 ～ +1 のオフセットへ変換する。
		float2 noiseUV = uv * g_distortionParam0.zw + g_distortionParam1.xy * g_distortionTime;
		float2 noise = g_tex.Sample(g_ss, noiseUV).rg * 2.0f - 1.0f;

		// 左右端は影響を少し弱め、
		// 柱状の揺らぎとして見えやすくしている。
		float columnMask = saturate(1.0f - abs(uv.x * 2.0f - 1.0f));
		mask *= columnMask;

		offset = noise * g_distortionStrength * mask;
	}
	// 衝撃波表現。
	else if (g_distortionType == 2)
	{
		// UV を中心基準の -1 ～ +1 空間へ変換し、
		// そこから半径と方向を計算する。
		float2 centeredUV = uv * 2.0f - 1.0f;
		float radius = g_distortionParam0.x;
		float thickness = g_distortionParam0.y;

		float shockMask = CalcShockWaveMask(centeredUV, radius, thickness);
		float centeredLength = length(centeredUV);
		float2 dir = centeredLength > 0.0001f ? centeredUV / centeredLength : 0.0f;

		mask *= shockMask;
		offset = dir * g_distortionStrength * mask;
	}

	// RG に UV のずらし量、A に歪み適用率を書き出す。
	// 実際に背景を曲げるのは PostProcess 側の最終合成。
	return float4(offset, 0.0f, mask);
}
