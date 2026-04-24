#include "../inc_KdCommon.hlsli"
#include "inc_KdPostProcessShader.hlsli"

// t0には、シーンを描いた時の深度画像が入る。
// 深度を見ることで、空と遮蔽物の位置を見分ける。
Texture2D g_depthTex : register(t0);
SamplerState g_ss : register(s0);

//------------------------------
// 定数バッファ(ディストーション)
//------------------------------
// LightShaftの見た目を調整する値。
// C++側のcbLightShaftと同じ順番で並べる必要がある。
cbuffer cb : register(b0)
{
	float g_density;			// 太陽方向へどれだけ広くサンプリングするか。
	float g_weight;				// 1回のサンプリングで加算する光の量。
	float g_decay;				// 太陽から離れるほど光を弱めるための倍率。
	float g_strength;			// 最後に掛ける全体の強さ。

	float3 g_color;				// LightShaftの色。
	float _blank;				// 定数バッファの16バイト境界を合わせるための余白。

	float g_sunDistance;		// カメラから仮想の太陽位置までの距離。
	float g_depthThreshold;		// この深度より手前を遮蔽物として扱う境界。
	float g_screenFadeStart;		// 太陽が画面端へ近づいた時に薄くし始める距離。
	float g_screenFadeEnd;		// この距離より外側ではLightShaftを消す。
};

float4 main(VSOutput In) : SV_Target0
{
	// 現在のピクセルから太陽方向へ、何回深度を調べるか。
	const int kSampleCount = 40;

	// ライト方向が未設定だと太陽位置を決められないため、何も描かない。
	float dirLength = length(g_DL_Dir);
	if (dirLength <= 0.0001f)
	{
		return 0;
	}

	// 平行光の向きから、カメラの前方に仮想の太陽位置を作る。
	float3 sunWorldPos = g_CamPos - normalize(g_DL_Dir) * g_sunDistance;
	// 太陽位置をカメラから見た座標へ変換する。
	float4 sunViewPos = mul(float4(sunWorldPos, 1), g_mView);
	// 太陽がカメラの後ろにある場合は、画面に光の筋を出さない。
	if (sunViewPos.z <= 0.0f)
	{
		return 0;
	}

	// 画面上のどこに太陽があるかを求めるため、射影変換する。
	float4 sunClipPos = mul(sunViewPos, g_mProj);
	if (sunClipPos.w <= 0.0f)
	{
		return 0;
	}

	// クリップ座標からUV座標へ変換する。UVは左上が(0,0)、右下が(1,1)。
	float2 sunNdc = sunClipPos.xy / sunClipPos.w;
	float2 sunUV = sunNdc * float2(0.5f, -0.5f) + 0.5f;

	// 太陽が画面外へ離れるほどLightShaftを薄くする。
	float screenDistance = length(sunUV * 2.0f - 1.0f);
	float screenFade = 1.0f - smoothstep(g_screenFadeStart, g_screenFadeEnd, screenDistance);
	if (screenFade <= 0.0f)
	{
		return 0;
	}

	// 今描いているピクセルが物体上かどうかを深度で判定する。
	float currentDepth = g_depthTex.Sample(g_ss, In.UV).r;
	float currentOccluder = (currentDepth < g_depthThreshold) ? 1.0f : 0.0f;

	// 現在のピクセルから太陽位置へ向かって、少しずつ調べるための移動量。
	float2 rayStep = (sunUV - In.UV) * (g_density / kSampleCount);
	float2 sampleUV = In.UV;

	// shaftは光の筋の強さ、decayはサンプルごとの減衰率。
	float shaft = 0.0f;
	float decay = 1.0f;

	[unroll]
	for (int i = 0; i < kSampleCount; ++i)
	{
		// 太陽方向へ一歩進めた位置の深度を調べる。
		sampleUV += rayStep;

		float sampleDepth = g_depthTex.Sample(g_ss, sampleUV).r;
		// 物体がある位置を、光の筋を作るための情報として扱う。
		float occluder = (sampleDepth < g_depthThreshold) ? 1.0f : 0.0f;

		// 遮蔽物があるほど光の筋の材料を加算し、遠いサンプルほど弱める。
		shaft += occluder * decay * g_weight;
		decay *= g_decay;
	}

	// 全体の強さ、画面端フェード、現在ピクセルが遮蔽物でないことを反映する。
	shaft *= g_strength;
	shaft *= screenFade;
	shaft *= (1.0f - currentOccluder);

	// rgbには光の色、alphaには光の筋の強さを入れる。
	// 後段のDoFシェーダーが、このalphaを使って画面へ合成する。
	return float4(g_color, shaft);
}
