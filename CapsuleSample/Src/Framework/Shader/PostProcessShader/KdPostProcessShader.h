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
	void SetLightShaftStrength(float strength) { m_cb0_LightShaftInfo.Work().Strength = strength; }
	void SetLightShaftColor(const Math::Vector3& color) { m_cb0_LightShaftInfo.Work().Color = color; }
	void AddSSPRPlane(const Math::Vector3& pos, const Math::Vector3& normal,
		const Math::Vector3& right, const Math::Vector3& up,
		float halfWidth, float halfHeight, float reflectionStrength, float roughness);

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
	void LightShaftProcess();
	void DepthOfFieldProcess();
	void SSPRProcess();

	void CreateBlurOffsetList(std::vector<Math::Vector3>& dstInfo, const std::shared_ptr<KdTexture>& spSrcTex, int samplingSize, const Math::Vector2& dir);

	void DrawTexture(std::shared_ptr<KdTexture>* spSrcTex, int srcTexSize, std::shared_ptr<KdTexture> spDstTex, D3D11_VIEWPORT* pVP);

	void SetBlurInfo(const std::shared_ptr<KdTexture>& spSrcTex, int samplingSize, const Math::Vector2& dir);
	void SetBlurInfo(const std::vector<Math::Vector3>& srcInfo);

	void SetBlurToDevice();
	void SetDoFToDevice();
	void SetBrightToDevice();
	void SetLightShaftToDevice();
	void SetSSPRResolveToDevice();

	std::shared_ptr<KdTexture> GetWorkingSceneTex() const;

	ID3D11VertexShader* m_VS = nullptr;
	ID3D11InputLayout* m_inputLayout = nullptr;

	ID3D11PixelShader* m_PS_Blur = nullptr;
	ID3D11PixelShader* m_PS_DoF = nullptr;
	ID3D11PixelShader* m_PS_Bright = nullptr;
	ID3D11PixelShader* m_PS_LightShaft = nullptr;
	ID3D11PixelShader* m_PS_SSPRResolve = nullptr;
	ID3D11ComputeShader* m_CS_SSPRProject = nullptr;

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

	struct cbLightShaft
	{
		float Density = 0.96f;
		float Weight = 0.08f;
		float Decay = 0.95f;
		float Strength = 0.35f;

		Math::Vector3 Color = Math::Vector3::Zero;
		float _blank = 0.0f;

		float SunDistance = 60.0f;
		float DepthThreshold = 0.9995f;
		float ScreenFadeStart = 1.0f;
		float ScreenFadeEnd = 1.35f;
	};
	KdConstantBuffer<cbLightShaft>	m_cb0_LightShaftInfo;

	static const int kMaxSSPRMirrorNum = 16;
	struct cbSSPR
	{
		Math::Matrix View = Math::Matrix::Identity;
		Math::Matrix Proj = Math::Matrix::Identity;
		Math::Matrix ProjInv = Math::Matrix::Identity;
		Math::Matrix ViewInv = Math::Matrix::Identity;

		Math::Vector2 ScreenSize = { 1.0f, 1.0f };
		Math::Vector2 InvScreenSize = { 1.0f, 1.0f };
		int MirrorCount = 0;
		float _blank0[3] = { 0.0f, 0.0f, 0.0f };

		// 1枚ごとの鏡面情報を float4 に詰めておくと、C++ と HLSL の並びを揃えやすい。
		Math::Vector4 PlanePosStrength[kMaxSSPRMirrorNum] = {};
		Math::Vector4 PlaneNormalBias[kMaxSSPRMirrorNum] = {};
		Math::Vector4 PlaneRightHalfWidth[kMaxSSPRMirrorNum] = {};
		Math::Vector4 PlaneUpHalfHeight[kMaxSSPRMirrorNum] = {};
		Math::Vector4 PlaneParams[kMaxSSPRMirrorNum] = {};
	};
	KdConstantBuffer<cbSSPR>	m_cb1_SSPRInfo;

	KdRenderTargetPack	m_postEffectRTPack;
	KdRenderTargetPack	m_ssprRTPack;

	KdRenderTargetPack	m_blurRTPack;
	KdRenderTargetPack	m_strongBlurRTPack;

	KdRenderTargetPack	m_depthOfFieldRTPack;

	KdRenderTargetPack	m_brightEffectRTPack;
	KdRenderTargetPack	m_lightShaftRTPack;
	static const int	kLightBloomNum = 4;
	KdRenderTargetPack	m_lightBloomRTPack[kLightBloomNum];

	KdRenderTargetChanger m_postEffectRTChanger;
	KdRenderTargetChanger m_brightRTChanger;

	Vertex m_screenVert[4];

	// CS が投影先対応表を書き込み、PS がそれを読んで反射色へ戻す。
	ID3D11Texture2D* m_ssprInfoTex = nullptr;
	ID3D11ShaderResourceView* m_ssprInfoSRV = nullptr;
	ID3D11UnorderedAccessView* m_ssprInfoUAV = nullptr;

	bool m_useSSPRScene = false;
};
