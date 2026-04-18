#include "KdPostProcessShader.h"

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// シェーダー本体の生成、定数バッファの生成
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdPostProcessShader::Init()
{
	// VS と InputLayout作成
	{
#include "KdPostProcessShader_VS.shaderInc"

		if (FAILED(KdDirect3D::Instance().WorkDev()->CreateVertexShader(compiledBuffer, sizeof(compiledBuffer), nullptr, &m_VS))) {
			assert(0 && "頂点シェーダー作成失敗");
			Release();
			return false;
		}

		std::vector<D3D11_INPUT_ELEMENT_DESC> layout = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,		0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,			0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};

		if (FAILED(KdDirect3D::Instance().WorkDev()->CreateInputLayout(
			&layout[0], (UINT)layout.size(), compiledBuffer,
			sizeof(compiledBuffer), &m_inputLayout)) ) 
		{
			assert(0 && "CreateInputLayout失敗");
			Release();
			return false;
		}
	}

	// PS 作成
	{
#include "KdPostProcessShader_PS_Blur.shaderInc"

		if (FAILED(KdDirect3D::Instance().WorkDev()->CreatePixelShader(
			compiledBuffer, sizeof(compiledBuffer), nullptr, &m_PS_Blur))) 
		{
			assert(0 && "ピクセルシェーダー作成失敗");
			Release();
			
			return false;
		}

	}

	{
#include "KdPostProcessShader_PS_DoF.shaderInc"

		if (FAILED(KdDirect3D::Instance().WorkDev()->CreatePixelShader(
			compiledBuffer, sizeof(compiledBuffer), nullptr, &m_PS_DoF))) 
		{
			assert(0 && "ピクセルシェーダー作成失敗");
			Release();

			return false;
		}
	}

	{
#include "KdPostProcessShader_PS_Bright.shaderInc"

		if (FAILED(KdDirect3D::Instance().WorkDev()->CreatePixelShader(
			compiledBuffer, sizeof(compiledBuffer), nullptr, &m_PS_Bright))) 
		{
			assert(0 && "ピクセルシェーダー作成失敗");
			Release();

			return false;
		}
	}

	{
#include "KdPostProcessShader_PS_LightShaft.shaderInc"

		if (FAILED(KdDirect3D::Instance().WorkDev()->CreatePixelShader(
			compiledBuffer, sizeof(compiledBuffer), nullptr, &m_PS_LightShaft)))
		{
			assert(0 && "ピクセルシェーダー作成失敗");
			Release();

			return false;
		}
	}

	{
#include "KdPostProcessShader_PS_SSPRResolve.shaderInc"

		if (FAILED(KdDirect3D::Instance().WorkDev()->CreatePixelShader(
			compiledBuffer, sizeof(compiledBuffer), nullptr, &m_PS_SSPRResolve)))
		{
			assert(0 && "ピクセルシェーダー作成失敗");
			Release();

			return false;
		}
	}

	{
#include "KdPostProcessShader_CS_SSPRProject.shaderInc"

		if (FAILED(KdDirect3D::Instance().WorkDev()->CreateComputeShader(
			compiledBuffer, sizeof(compiledBuffer), nullptr, &m_CS_SSPRProject)))
		{
			assert(0 && "コンピュートシェーダー作成失敗");
			Release();

			return false;
		}
	}

	m_cb0_BlurInfo.Create();

	m_cb0_DoFInfo.Create();

	m_cb0_BrightInfo.Create();

	m_cb0_LightShaftInfo.Create();
	m_cb1_SSPRInfo.Create();

	const std::shared_ptr<KdTexture>& backBuffer = KdDirect3D::Instance().GetBackBuffer();
	
	// ポストプロセス用のシーンの全描画用画像
	m_postEffectRTPack.CreateRenderTarget(backBuffer->GetWidth(), backBuffer->GetHeight(), true);
	m_ssprRTPack.CreateRenderTarget(backBuffer->GetWidth(), backBuffer->GetHeight());

	// ぼかし画像
	m_blurRTPack.CreateRenderTarget(backBuffer->GetWidth(), backBuffer->GetHeight());
	m_strongBlurRTPack.CreateRenderTarget(backBuffer->GetWidth() / 2, backBuffer->GetHeight() / 2);

	// 被写界深度画像
	m_depthOfFieldRTPack.CreateRenderTarget(backBuffer->GetWidth(), backBuffer->GetHeight());
	
	m_brightEffectRTPack.CreateRenderTarget(backBuffer->GetWidth(), backBuffer->GetHeight());

	m_lightShaftRTPack.CreateRenderTarget(backBuffer->GetWidth(), backBuffer->GetHeight());

	int lightBloomWidth = m_brightEffectRTPack.m_RTTexture->GetWidth();
	int lightBloomHeight = m_brightEffectRTPack.m_RTTexture->GetHeight();

	// 光源ぼかし画像
	for (int i = 0; i < kLightBloomNum; ++i)
	{
		m_lightBloomRTPack[i].CreateRenderTarget(lightBloomWidth, lightBloomHeight);

		lightBloomWidth /= 2;
		lightBloomHeight /= 2;
	}

	{
		// ComputeShader は「どの画素がどの鏡面画素へ対応するか」だけを別テクスチャへ書き出す。
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = backBuffer->GetWidth();
		desc.Height = backBuffer->GetHeight();
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R32_UINT;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		if (FAILED(KdDirect3D::Instance().WorkDev()->CreateTexture2D(&desc, nullptr, &m_ssprInfoTex)))
		{
			assert(0 && "SSPR情報テクスチャ作成失敗");
			Release();
			return false;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = desc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;

		if (FAILED(KdDirect3D::Instance().WorkDev()->CreateShaderResourceView(m_ssprInfoTex, &srvDesc, &m_ssprInfoSRV)))
		{
			assert(0 && "SSPR情報SRV作成失敗");
			Release();
			return false;
		}

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = desc.Format;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;

		if (FAILED(KdDirect3D::Instance().WorkDev()->CreateUnorderedAccessView(m_ssprInfoTex, &uavDesc, &m_ssprInfoUAV)))
		{
			assert(0 && "SSPR情報UAV作成失敗");
			Release();
			return false;
		}
	}

	// 画面全体に書き込む用の頂点情報
	m_screenVert[0] = { {-1,-1,0}, {0, 1} };
	m_screenVert[1] = { {-1, 1,0}, {0, 0} };
	m_screenVert[2] = { { 1,-1,0}, {1, 1} };
	m_screenVert[3] = { { 1, 1,0}, {1, 0} };

	SetBrightThreshold( 1.2f );

	return true;
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// シェーダー本体の解放、定数バッファの解放
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdPostProcessShader::Release()
{
	KdSafeRelease(m_VS);

	KdSafeRelease(m_inputLayout);

	KdSafeRelease(m_PS_Blur);
	KdSafeRelease(m_PS_DoF);
	KdSafeRelease(m_PS_Bright);
	KdSafeRelease(m_PS_LightShaft);
	KdSafeRelease(m_PS_SSPRResolve);
	KdSafeRelease(m_CS_SSPRProject);
	KdSafeRelease(m_ssprInfoSRV);
	KdSafeRelease(m_ssprInfoUAV);
	KdSafeRelease(m_ssprInfoTex);

	m_cb0_BlurInfo.Release();
	m_cb0_DoFInfo.Release();
	m_cb0_BrightInfo.Release();
	m_cb0_LightShaftInfo.Release();
	m_cb1_SSPRInfo.Release();
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// 
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdPostProcessShader::Draw()
{
	m_cb1_SSPRInfo.Work().MirrorCount = 0;
	m_useSSPRScene = false;

	// ポストエフェクトテクスチャの描画クリア
	m_postEffectRTPack.ClearTexture();
	m_ssprRTPack.ClearTexture();

	// 光源描画テクスチャの描画クリア
	m_brightEffectRTPack.ClearTexture(kBlackColor);

	// レンダーターゲット変更
	if (!m_postEffectRTChanger.ChangeRenderTarget(m_postEffectRTPack))
	{
		// 失敗したらUndo
		m_postEffectRTChanger.UndoRenderTarget();
	}
}

void KdPostProcessShader::AddSSPRPlane(const Math::Vector3& pos, const Math::Vector3& normal,
	const Math::Vector3& right, const Math::Vector3& up,
	float halfWidth, float halfHeight, float reflectionStrength, float roughness)
{
	auto& sspr = m_cb1_SSPRInfo.Work();
	if (sspr.MirrorCount >= kMaxSSPRMirrorNum) return;

	const int mirrorIndex = sspr.MirrorCount;
	sspr.PlanePosStrength[mirrorIndex] = { pos.x, pos.y, pos.z, reflectionStrength };
	sspr.PlaneNormalBias[mirrorIndex] = { normal.x, normal.y, normal.z, 0.05f };
	sspr.PlaneRightHalfWidth[mirrorIndex] = { right.x, right.y, right.z, halfWidth };
	sspr.PlaneUpHalfHeight[mirrorIndex] = { up.x, up.y, up.z, halfHeight };
	sspr.PlaneParams[mirrorIndex] = { roughness, 0.0f, 0.0f, 0.0f };

	sspr.MirrorCount++;
}

std::shared_ptr<KdTexture> KdPostProcessShader::GetWorkingSceneTex() const
{
	return m_useSSPRScene ? m_ssprRTPack.m_RTTexture : m_postEffectRTPack.m_RTTexture;
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// 
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdPostProcessShader::BeginBright()
{
	if (!m_brightRTChanger.ChangeRenderTarget(m_brightEffectRTPack.m_RTTexture, m_postEffectRTPack.m_ZBuffer, &m_brightEffectRTPack.m_viewPort))
	{
		m_brightRTChanger.UndoRenderTarget();
	}

	KdShaderManager::Instance().ChangeBlendState(KdBlendState::Add);

	KdShaderManager::Instance().ChangeDepthStencilState(KdDepthStencilState::ZWriteDisable);
}

void KdPostProcessShader::EndBright()
{
	KdShaderManager::Instance().UndoDepthStencilState();

	KdShaderManager::Instance().UndoBlendState();

	m_brightRTChanger.UndoRenderTarget();
}

void KdPostProcessShader::PostEffectProcess()
{
	m_postEffectRTChanger.UndoRenderTarget();

	// 鏡面反射は scene color を作り替える処理なので、他の post effect より先に行う。
	SSPRProcess();
	LightBloomProcess();
	LightShaftProcess();
	BlurProcess();
	DepthOfFieldProcess();

	KdShaderManager::Instance().m_spriteShader.DrawTex(m_depthOfFieldRTPack.m_RTTexture.get(), 0, 0);
}

void KdPostProcessShader::LightBloomProcess()
{
	SetBrightToDevice();

	KdShaderManager::Instance().ChangeBlendState(KdBlendState::Add);

	// 高輝度抽出
	std::shared_ptr<KdTexture> sceneTex = GetWorkingSceneTex();
	DrawTexture(&sceneTex, 1, m_brightEffectRTPack.m_RTTexture, &m_brightEffectRTPack.m_viewPort);

	KdShaderManager::Instance().UndoBlendState();

	// LightBloom画像の作成
	SetBlurToDevice();

	std::shared_ptr<KdTexture> srcRTTex = m_brightEffectRTPack.m_RTTexture;

	for (int i = 0; i < kLightBloomNum; ++i)
	{
		GenerateBlurTexture(srcRTTex, m_lightBloomRTPack[i].m_RTTexture, m_lightBloomRTPack[i].m_viewPort, kBlurSamplingRadius);
			
		srcRTTex = m_lightBloomRTPack[i].m_RTTexture;
	}

	KdRenderTargetChanger RTChanger;
	RTChanger.ChangeRenderTarget(sceneTex, nullptr, &m_postEffectRTPack.m_viewPort);

	KdShaderManager::Instance().ChangeSamplerState(KdSamplerState::Linear_Clamp);

	KdShaderManager::Instance().ChangeBlendState(KdBlendState::Add);

	// 光源ぼかし画像の合成
	for (int i = 0; i < kLightBloomNum; ++i)
	{
		KdShaderManager::Instance().m_spriteShader.DrawTex(m_lightBloomRTPack[i].m_RTTexture.get(), 0, 0, sceneTex->GetWidth(), sceneTex->GetHeight());
	}

	RTChanger.UndoRenderTarget();

	KdShaderManager::Instance().UndoBlendState();

	KdShaderManager::Instance().UndoSamplerState();
}

void KdPostProcessShader::BlurProcess()
{
	SetBlurToDevice();

	std::shared_ptr<KdTexture> sceneTex = GetWorkingSceneTex();
	GenerateBlurTexture(sceneTex, m_blurRTPack.m_RTTexture, m_blurRTPack.m_viewPort, kBlurSamplingRadius);

	GenerateBlurTexture(m_blurRTPack.m_RTTexture, m_strongBlurRTPack.m_RTTexture, m_strongBlurRTPack.m_viewPort, kBlurSamplingRadius);
}

void KdPostProcessShader::DepthOfFieldProcess()
{
	SetDoFToDevice();

	std::shared_ptr<KdTexture> srcTexList[5] =
	{
		GetWorkingSceneTex(),
		m_blurRTPack.m_RTTexture,
		m_strongBlurRTPack.m_RTTexture,
		m_postEffectRTPack.m_ZBuffer,
		m_lightShaftRTPack.m_RTTexture
	};

	DrawTexture(srcTexList, 5, m_depthOfFieldRTPack.m_RTTexture, &m_depthOfFieldRTPack.m_viewPort);
}

void KdPostProcessShader::LightShaftProcess()
{
	SetLightShaftToDevice();

	KdShaderManager::Instance().ChangeSamplerState(KdSamplerState::Point_Clamp);

	m_lightShaftRTPack.ClearTexture(kBlueColor);

	DrawTexture(&m_postEffectRTPack.m_ZBuffer, 1, m_lightShaftRTPack.m_RTTexture, &m_lightShaftRTPack.m_viewPort);

	KdShaderManager::Instance().UndoSamplerState();
}

void KdPostProcessShader::SSPRProcess()
{
	if (m_cb1_SSPRInfo.Work().MirrorCount <= 0) return;
	if (!m_CS_SSPRProject || !m_PS_SSPRResolve) return;
	if (!m_ssprInfoUAV || !m_ssprInfoSRV) return;

	const auto& camera = KdShaderManager::Instance().GetCameraCB();
	auto& ssprInfo = m_cb1_SSPRInfo.Work();

	ssprInfo.View = camera.mView;
	ssprInfo.Proj = camera.mProj;
	ssprInfo.ProjInv = camera.mProjInv;
	ssprInfo.ViewInv = camera.mView.Invert();
	ssprInfo.ScreenSize = {
		static_cast<float>(m_postEffectRTPack.m_RTTexture->GetWidth()),
		static_cast<float>(m_postEffectRTPack.m_RTTexture->GetHeight())
	};
	ssprInfo.InvScreenSize = {
		1.0f / ssprInfo.ScreenSize.x,
		1.0f / ssprInfo.ScreenSize.y
	};
	m_cb1_SSPRInfo.Write();

	ID3D11DeviceContext* devCon = KdDirect3D::Instance().WorkDevContext();
	if (!devCon) return;

	UINT clearValues[4] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF };
	devCon->ClearUnorderedAccessViewUint(m_ssprInfoUAV, clearValues);

	// 1段目の CS は、元画面の各画素を鏡で反転させたときの投影先だけを作る。
	devCon->CSSetShader(m_CS_SSPRProject, nullptr, 0);
	devCon->CSSetConstantBuffers(1, 1, m_cb1_SSPRInfo.GetAddress());
	devCon->CSSetShaderResources(0, 1, m_postEffectRTPack.m_ZBuffer->WorkSRViewAddress());
	devCon->CSSetUnorderedAccessViews(0, 1, &m_ssprInfoUAV, nullptr);

	const UINT groupX = (m_postEffectRTPack.m_RTTexture->GetWidth() + 7) / 8;
	const UINT groupY = (m_postEffectRTPack.m_RTTexture->GetHeight() + 7) / 8;
	devCon->Dispatch(groupX, groupY, 1);

	ID3D11ShaderResourceView* nullSRV = nullptr;
	ID3D11UnorderedAccessView* nullUAV = nullptr;
	devCon->CSSetShaderResources(0, 1, &nullSRV);
	devCon->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
	devCon->CSSetShader(nullptr, nullptr, 0);

	// 2段目の PS は、投影結果と roughness を使って最終的な反射色へ戻す。
	SetSSPRResolveToDevice();

	KdRenderTargetChanger rtChanger;
	rtChanger.ChangeRenderTarget(m_ssprRTPack);

	KdShaderManager::Instance().ChangeSamplerState(KdSamplerState::Linear_Clamp);

	devCon->PSSetShaderResources(0, 1, m_postEffectRTPack.m_RTTexture->WorkSRViewAddress());
	devCon->PSSetShaderResources(1, 1, &m_ssprInfoSRV);
	devCon->PSSetShaderResources(2, 1, m_postEffectRTPack.m_ZBuffer->WorkSRViewAddress());

	KdDirect3D::Instance().DrawVertices(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, 4, &m_screenVert[0], sizeof(Vertex));

	devCon->PSSetShaderResources(0, 1, &nullSRV);
	devCon->PSSetShaderResources(1, 1, &nullSRV);
	devCon->PSSetShaderResources(2, 1, &nullSRV);

	KdShaderManager::Instance().UndoSamplerState();

	rtChanger.UndoRenderTarget();

	m_useSSPRScene = true;
}

void KdPostProcessShader::CreateBlurOffsetList(std::vector<Math::Vector3>& dstInfo, const std::shared_ptr<KdTexture>& spSrcTex, int samplingRadius, const Math::Vector2& dir)
{
	Math::Vector2 blurDir = dir;
	blurDir.Normalize();

	// 両サイドのサンプリング回数 ＋ サンプル開始中央のピクセル
	int totalSamplingNum = samplingRadius * 2 + 1;

	// サンプリングするテクセルのオフセット値
	Math::Vector2 texelSize;
	texelSize.x = 1.0f / spSrcTex->GetWidth();
	texelSize.y = 1.0f / spSrcTex->GetHeight();

	dstInfo.resize(totalSamplingNum);

	float totalWeight = 0;
	for (int i = 0; i < totalSamplingNum; ++i)
	{
		int samplingOffset = i - samplingRadius;
		dstInfo[i].x = blurDir.x * (samplingOffset * texelSize.x);
		dstInfo[i].y = blurDir.y * (samplingOffset * texelSize.y);

		// 中心のピクセルのウェイトが大きくなる計算
		float weight = exp(-(samplingOffset * samplingOffset) / 18.0f);

		// サンプリングする各ピクセルに重みをつける
		dstInfo[i].z = weight;
		totalWeight += weight;
	}

	// ウェイトを全体のウェイトから割り算し、各ピクセルのウェイトの意味を割合に置き換える
	// 全部足して1になるように数値を調整する
	for (int i = 0; i < totalSamplingNum; ++i)
	{
		dstInfo[i].z /= totalWeight;
	}
}

void KdPostProcessShader::GenerateBlurTexture(std::shared_ptr<KdTexture>& spSrcTex, std::shared_ptr<KdTexture>& spDstTex, D3D11_VIEWPORT& VP, int blurRadius)
{
	KdShaderManager::Instance().ChangeSamplerState(KdSamplerState::Linear_Clamp);

	KdRenderTargetPack tmpBlurRTPack;
	tmpBlurRTPack.CreateRenderTarget(spDstTex->GetWidth(), spDstTex->GetHeight());

	// 横にぼかす
	std::vector<Math::Vector3> horizontalBlurInfo;
	CreateBlurOffsetList(horizontalBlurInfo, spDstTex, blurRadius, { 1.0f, 0 });
	SetBlurInfo(horizontalBlurInfo);

	DrawTexture(&spSrcTex, 1, tmpBlurRTPack.m_RTTexture, &tmpBlurRTPack.m_viewPort);

	// 横にぼかした画像を更に縦にぼかす
	std::vector<Math::Vector3> verticalBlurInfo;
	CreateBlurOffsetList(verticalBlurInfo, spDstTex, blurRadius, { 0, 1.0f });
	SetBlurInfo(verticalBlurInfo);

	DrawTexture(&tmpBlurRTPack.m_RTTexture, 1, spDstTex, &VP);

	KdShaderManager::Instance().UndoSamplerState();
}

void KdPostProcessShader::DrawTexture(std::shared_ptr<KdTexture>* spSrcTex, int srcTexSize, std::shared_ptr<KdTexture> spDstTex, D3D11_VIEWPORT* pVP)
{
	if (!spSrcTex) { return; }

	KdRenderTargetChanger RTChanger;

	if (spDstTex)
	{
		RTChanger.ChangeRenderTarget(spDstTex, nullptr, pVP);
	}

	ID3D11DeviceContext* pDevCon = KdDirect3D::Instance().WorkDevContext();

	// SRVのセット
	for (int i = 0; i < srcTexSize; ++i)
	{
		pDevCon->PSSetShaderResources(i, 1, spSrcTex[i]->WorkSRViewAddress());
	}

	// テクスチャーの描画
	KdDirect3D::Instance().DrawVertices(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, 4, &m_screenVert[0], sizeof(Vertex));

	// SRVの解放
	ID3D11ShaderResourceView* nullSRV = nullptr;

	for (int i = 0; i < srcTexSize; ++i)
	{
		pDevCon->PSSetShaderResources(i, 1, &nullSRV);
	}

	RTChanger.UndoRenderTarget();
}

void KdPostProcessShader::SetBlurInfo(const std::shared_ptr<KdTexture>& spSrcTex, int samplingRadius, const Math::Vector2& dir)
{
	std::vector<Math::Vector3> blurOffsetList;

	CreateBlurOffsetList(blurOffsetList, spSrcTex, samplingRadius, dir);

	SetBlurInfo(blurOffsetList);
}

void KdPostProcessShader::SetBlurInfo(const std::vector<Math::Vector3>& srcInfo)
{
	KdPostProcessShader::cbBlur& blurInfo = m_cb0_BlurInfo.Work();

	blurInfo.SamplingNum = (signed)srcInfo.size();

	if (blurInfo.SamplingNum > kMaxSampling)
	{
		assert(0 && "サンプリング指定回数が上限を超えています。");

		blurInfo.SamplingNum = 0;

		return;
	}

	for (int i = 0; i < blurInfo.SamplingNum; ++i)
	{
		blurInfo.Info[i].x = srcInfo[i].x;
		blurInfo.Info[i].y = srcInfo[i].y;
		blurInfo.Info[i].z = srcInfo[i].z;
	}

	m_cb0_BlurInfo.Write();
}

void KdPostProcessShader::SetBlurToDevice()
{
	ID3D11DeviceContext* DevCon = KdDirect3D::Instance().WorkDevContext();
	if (!DevCon) { return; }

	m_cb0_BlurInfo.Write();

	KdDirect3D::Instance().WorkDevContext()->PSSetConstantBuffers(0, 1, m_cb0_BlurInfo.GetAddress());

	KdShaderManager& shaderMgr = KdShaderManager::Instance();

	if (shaderMgr.SetVertexShader(m_VS))
	{
		DevCon->IASetInputLayout(m_inputLayout);
	}

	shaderMgr.SetPixelShader(m_PS_Blur);
}

void KdPostProcessShader::SetDoFToDevice()
{
	ID3D11DeviceContext* DevCon = KdDirect3D::Instance().WorkDevContext();
	if (!DevCon) { return; }

	m_cb0_DoFInfo.Write();

	KdDirect3D::Instance().WorkDevContext()->PSSetConstantBuffers(0, 1, m_cb0_DoFInfo.GetAddress());

	KdShaderManager& shaderMgr = KdShaderManager::Instance();

	if (shaderMgr.SetVertexShader(m_VS))
	{
		DevCon->IASetInputLayout(m_inputLayout);
	}

	shaderMgr.SetPixelShader(m_PS_DoF);
}

void KdPostProcessShader::SetBrightToDevice()
{
	ID3D11DeviceContext* DevCon = KdDirect3D::Instance().WorkDevContext();
	if (!DevCon) { return; }

	m_cb0_BrightInfo.Write();

	KdDirect3D::Instance().WorkDevContext()->PSSetConstantBuffers(0, 1, m_cb0_BrightInfo.GetAddress());

	KdShaderManager& shaderMgr = KdShaderManager::Instance();

	if (shaderMgr.SetVertexShader(m_VS))
	{
		DevCon->IASetInputLayout(m_inputLayout);
	}

	shaderMgr.SetPixelShader(m_PS_Bright);
}

void KdPostProcessShader::SetLightShaftToDevice()
{
	ID3D11DeviceContext* DevCon = KdDirect3D::Instance().WorkDevContext();
	if (!DevCon) { return; }

	m_cb0_LightShaftInfo.Write();

	KdDirect3D::Instance().WorkDevContext()->PSSetConstantBuffers(0, 1, m_cb0_LightShaftInfo.GetAddress());

	KdShaderManager& shaderMgr = KdShaderManager::Instance();

	if (shaderMgr.SetVertexShader(m_VS))
	{
		DevCon->IASetInputLayout(m_inputLayout);
	}

	shaderMgr.SetPixelShader(m_PS_LightShaft);
}

void KdPostProcessShader::SetSSPRResolveToDevice()
{
	ID3D11DeviceContext* DevCon = KdDirect3D::Instance().WorkDevContext();
	if (!DevCon) { return; }

	m_cb1_SSPRInfo.Write();

	DevCon->PSSetConstantBuffers(1, 1, m_cb1_SSPRInfo.GetAddress());

	KdShaderManager& shaderMgr = KdShaderManager::Instance();

	if (shaderMgr.SetVertexShader(m_VS))
	{
		DevCon->IASetInputLayout(m_inputLayout);
	}

	shaderMgr.SetPixelShader(m_PS_SSPRResolve);
}
