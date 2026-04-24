#include "../inc_KdCommon.hlsli"
#include "inc_KdPostProcessShader.hlsli"

Texture2D g_sceneTex : register(t0);
Texture2D g_distortionTex : register(t1);

SamplerState g_ss : register(s0);

float4 main(VSOutput In) : SV_Target0
{
	// 歪み RT から、各ピクセルの「どちらへずらすか」を読む。
	float4 distortion = g_distortionTex.Sample(g_ss, In.UV);

	// RG に入っているオフセット量だけ UV をずらす。
	// saturate しておく事で、画面外参照を防ぐ。
	float2 distortedUV = saturate(In.UV + distortion.rg);

	// 元の背景色と、ずらした先の背景色をそれぞれ取得する。
	float4 baseColor = g_sceneTex.Sample(g_ss, In.UV);
	float4 distortedColor = g_sceneTex.Sample(g_ss, distortedUV);

	// A に入っている適用率だけ歪み色を混ぜる。
	// A が 0 なら元画像、1 に近いほど完全に歪んだ画像を使う。
	return lerp(baseColor, distortedColor, saturate(distortion.a));
}
