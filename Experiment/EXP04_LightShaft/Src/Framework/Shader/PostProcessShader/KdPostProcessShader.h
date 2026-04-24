#pragma once

class KdPostProcessShader
{
public:
	KdPostProcessShader() {}
	~KdPostProcessShader()
	{
		Release();
	}

	void SetNearClippingDistance(float distance) { m_cb0_DoFInfo.Work().NearClippingDistance = distance; }
	void SetFarClippingDistance(float distance) { m_cb0_DoFInfo.Work().FarClippingDistance = distance; }
	void SetFocusDistance(float distance) { m_cb0_DoFInfo.Work().FocusDistance = distance; }
	void SetFocusRange(float fore, float back) { m_cb0_DoFInfo.Work().FocusForeRange = fore; m_cb0_DoFInfo.Work().FocusBackRange = back; }

	void SetBrightThreshold(float threshold) { m_cb0_BrightInfo.Work().Threshold = threshold; }
	// LightShaftの濃さを設定する。値が大きいほど光の筋が強く出る。
	void SetLightShaftStrength(float strength) { m_cb0_LightShaftInfo.Work().Strength = strength; }
	// LightShaftの色を設定する。太陽光らしい色を入れると光の筋の色になる。
	void SetLightShaftColor(const Math::Vector3& color) { m_cb0_LightShaftInfo.Work().Color = color; }

	struct Vertex
	{
		Math::Vector3 Pos;
		Math::Vector2 UV;
	};

	bool Init();

	void Release();

	void Draw();

	void BeginBright();
	void EndBright();

	void PostEffectProcess();

	void GenerateBlurTexture(std::shared_ptr<KdTexture>& spSrcTex, std::shared_ptr<KdTexture>& spDstTex, D3D11_VIEWPORT& VP, int blurRadius);

private:

	void BlurProcess();
	void LightBloomProcess();
	void DepthOfFieldProcess();
	// 深度画像を使って、LightShaftだけが描かれた中間画像を作る。
	void LightShaftProcess();

	void CreateBlurOffsetList(std::vector<Math::Vector3>& dstInfo, const std::shared_ptr<KdTexture>& spSrcTex, int samplingSize, const Math::Vector2& dir);

	void DrawTexture(std::shared_ptr<KdTexture>* spSrcTex, int srcTexSize, std::shared_ptr<KdTexture> spDstTex, D3D11_VIEWPORT* pVP);

	void SetBlurInfo(const std::shared_ptr<KdTexture>& spSrcTex, int samplingSize, const Math::Vector2& dir);
	void SetBlurInfo(const std::vector<Math::Vector3>& srcInfo);

	void SetBlurToDevice();
	void SetDoFToDevice();
	void SetBrightToDevice();
	// LightShaft用の定数バッファとシェーダーをGPUにセットする。
	void SetLightShaftToDevice();

	ID3D11VertexShader* m_VS = nullptr;
	ID3D11InputLayout* m_inputLayout = nullptr;

	ID3D11PixelShader* m_PS_Blur = nullptr;
	ID3D11PixelShader* m_PS_DoF = nullptr;
	ID3D11PixelShader* m_PS_Bright = nullptr;
	// 深度からLightShaftの強さを計算するピクセルシェーダー。
	ID3D11PixelShader* m_PS_LightShaft = nullptr;

	static const int kBlurSamplingRadius = 8;
	static const int kLightBloomSamplingRadius = 4;

	static const int kMaxSampling = 31;
	struct cbBlur
	{
		Math::Vector4 Info[kMaxSampling];
	
		int SamplingNum = 0;
		int _blank[3] = { 0, 0 ,0 };
	};
	KdConstantBuffer<cbBlur>	m_cb0_BlurInfo;

	struct cbDepthOfField
	{
		float NearClippingDistance = 0.0f;
		float FarClippingDistance = 1000.0f;

		float FocusDistance = 0.0f;
		float FocusForeRange = 0.0f;
		float FocusBackRange = 1000.0f;
		int   _blank[3] = { 0, 0, 0 };
	};
	KdConstantBuffer<cbDepthOfField>	m_cb0_DoFInfo;

	struct cbBrightFilter
	{
		float Threshold = 0.0f;
		int _blank[3] = { 0, 0, 0 };
	};
	KdConstantBuffer<cbBrightFilter>	m_cb0_BrightInfo;

	// LightShaftシェーダーへ渡す調整値。
	// HLSL側のcbuffer cbと並び順を合わせる必要がある。
	struct cbLightShaft
	{
		float Density = 0.96f;			// 太陽方向へどれだけ広くサンプリングするか。
		float Weight = 0.08f;			// 1回のサンプリングで加算する光の量。
		float Decay = 0.95f;			// 太陽から離れるほど光を弱めるための倍率。
		float Strength = 0.55f;			// 最後に掛ける全体の強さ。

		Math::Vector3 Color = Math::Vector3::Zero;	// LightShaftの色。
		float _blank = 0.0f;						// 定数バッファの16バイト境界を合わせるための余白。

		float SunDistance = 60.0f;		// カメラから仮想の太陽位置までの距離。
		float DepthThreshold = 0.9995f;	// この深度より手前を遮蔽物として扱う境界。
		float ScreenFadeStart = 1.0f;	// 太陽が画面端へ近づいた時に薄くし始める距離。
		float ScreenFadeEnd = 1.35f;		// この距離より外側ではLightShaftを消す。
	};
	// LightShaftの調整値をGPUへ送るための定数バッファ。
	KdConstantBuffer<cbLightShaft>	m_cb0_LightShaftInfo;

	KdRenderTargetPack	m_postEffectRTPack;

	KdRenderTargetPack	m_blurRTPack;
	KdRenderTargetPack	m_strongBlurRTPack;

	KdRenderTargetPack	m_depthOfFieldRTPack;

	KdRenderTargetPack	m_brightEffectRTPack;
	// LightShaftだけを描き込む中間レンダーターゲット。
	KdRenderTargetPack	m_lightShaftRTPack;
	static const int	kLightBloomNum = 4;
	KdRenderTargetPack	m_lightBloomRTPack[kLightBloomNum];

	KdRenderTargetChanger m_postEffectRTChanger;
	KdRenderTargetChanger m_brightRTChanger;

	Vertex m_screenVert[4];
};
