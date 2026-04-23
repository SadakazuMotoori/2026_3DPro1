#include "Framework/KdFramework.h"

#include "KdStandardShader.h"

namespace
{
bool GetTexture2DFromSRV(ID3D11ShaderResourceView* srv, ID3D11Texture2D** ppTex, D3D11_TEXTURE2D_DESC* pDesc = nullptr)
{
	if (!srv || !ppTex) { return false; }

	*ppTex = nullptr;

	ID3D11Resource* resource = nullptr;
	srv->GetResource(&resource);
	if (!resource) { return false; }

	HRESULT hr = resource->QueryInterface<ID3D11Texture2D>(ppTex);
	resource->Release();
	if (FAILED(hr) || !(*ppTex)) { return false; }

	if (pDesc)
	{
		(*ppTex)->GetDesc(pDesc);
	}

	return true;
}

bool CreateDecalTextureArray(KdTexture& dstTex, const D3D11_TEXTURE2D_DESC& srcDesc, int arraySize)
{
	if (arraySize <= 0) { return false; }

	// デカールのシェーダーは SampleLevel(..., 0) で mip0 のみを参照するため、
	// 配列テクスチャも 1 mip 構成で十分。
	size_t rowPitch = 0;
	size_t slicePitch = 0;
	if (FAILED(DirectX::ComputePitch(srcDesc.Format, srcDesc.Width, srcDesc.Height, rowPitch, slicePitch)))
	{
		return false;
	}

	// 先に全スライスを透明で初期化しておくことで、
	// コピー対象外のスライスは「何も貼られないデカール」として安全に扱える。
	std::vector<unsigned char> clearPixels(slicePitch * static_cast<size_t>(arraySize), 0);
	std::vector<D3D11_SUBRESOURCE_DATA> initData(static_cast<size_t>(arraySize));
	for (int i = 0; i < arraySize; ++i)
	{
		initData[i].pSysMem = clearPixels.data() + slicePitch * static_cast<size_t>(i);
		initData[i].SysMemPitch = static_cast<UINT>(rowPitch);
		initData[i].SysMemSlicePitch = static_cast<UINT>(slicePitch);
	}

	return dstTex.Create(static_cast<int>(srcDesc.Width), static_cast<int>(srcDesc.Height), srcDesc.Format,
		static_cast<UINT>(arraySize), initData.data());
}

bool RefreshDecalTextureArray(KdTexture& dstTex, const std::shared_ptr<KdTexture>(&srcTex)[KdStandardShader::maxDecalNum], int decalNum)
{
	dstTex.Release();
	if (decalNum <= 0) { return false; }

	// Texture2DArray は全スライスでサイズ・フォーマットを揃える必要があるため、
	// 最初に見つかった有効テクスチャを「配列全体の基準情報」として採用する。
	D3D11_TEXTURE2D_DESC baseDesc = {};
	ID3D11Texture2D* baseTex = nullptr;
	for (int i = 0; i < decalNum; ++i)
	{
		if (!srcTex[i]) { continue; }

		if (GetTexture2DFromSRV(srcTex[i]->WorkSRView(), &baseTex, &baseDesc))
		{
			break;
		}
	}
	if (!baseTex) { return false; }
	baseTex->Release();

	if (!CreateDecalTextureArray(dstTex, baseDesc, decalNum))
	{
		return false;
	}

	// ここから各デカールの元テクスチャを、対応する配列スライスへ 1 枚ずつコピーする。
	ID3D11Texture2D* dstArrayTex = nullptr;
	if (!GetTexture2DFromSRV(dstTex.WorkSRView(), &dstArrayTex))
	{
		dstTex.Release();
		return false;
	}

	for (int i = 0; i < decalNum; ++i)
	{
		if (!srcTex[i]) { continue; }

		D3D11_TEXTURE2D_DESC srcDesc = {};
		ID3D11Texture2D* srcArrayTex = nullptr;
		if (!GetTexture2DFromSRV(srcTex[i]->WorkSRView(), &srcArrayTex, &srcDesc))
		{
			continue;
		}

		// Texture2DArray は全スライスでサイズ・フォーマットを揃える必要があるため、
		// 条件が合わないテクスチャはコピーせず、初期化済みの透明スライスのまま残す。
		const bool canCopy =
			srcDesc.ArraySize == 1 &&
			srcDesc.Width == baseDesc.Width &&
			srcDesc.Height == baseDesc.Height &&
			srcDesc.Format == baseDesc.Format;

		if (canCopy)
		{
			// 配列テクスチャ側は mip0 しか持たないので、元テクスチャ側も subresource 0 だけをコピーする。
			KdDirect3D::Instance().WorkDevContext()->CopySubresourceRegion(
				dstArrayTex,
				D3D11CalcSubresource(0, static_cast<UINT>(i), 1),
				0, 0, 0,
				srcArrayTex,
				0,
				nullptr);
		}

		srcArrayTex->Release();
	}

	dstArrayTex->Release();

	return true;
}
}


//================================================
// 描画準備
//================================================

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// 陰影をつけるオブジェクトの描画の直前処理（不透明な物体やキャラクタの板ポリゴン）
// ===== ===== ===== ===== ===== ===== ===== ===== ===== ===== ===== =====
// シェーダーのパイプライン変更
// LitShaderで使用するリソースのバッファー設定
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdStandardShader::BeginLit()
{
	// 頂点シェーダーのパイプライン変更
	if (KdShaderManager::Instance().SetVertexShader(m_VS_Lit))
	{
		KdShaderManager::Instance().SetInputLayout(m_inputLayout);

		KdShaderManager::Instance().SetVSConstantBuffer(0, m_cb0_Obj.GetAddress());
		KdShaderManager::Instance().SetVSConstantBuffer(1, m_cb1_Mesh.GetAddress());
	}

	// ピクセルシェーダーのパイプライン変更
	if (KdShaderManager::Instance().SetPixelShader(m_PS_Lit))
	{
		KdShaderManager::Instance().SetPSConstantBuffer(0, m_cb0_Obj.GetAddress());
		KdShaderManager::Instance().SetPSConstantBuffer(2, m_cb2_Material.GetAddress());
		KdShaderManager::Instance().SetPSConstantBuffer(4, m_cb4_Decal.GetAddress());
	}

	// ボーン情報をセット(スキンメッシュ対応)
	KdShaderManager::Instance().SetVSConstantBuffer(3, m_cb3_Bone.GetAddress());

	// シャドウマップのテクスチャをセット
	KdDirect3D::Instance().WorkDevContext()->PSSetShaderResources(10, 1, m_depthMapFromLightRTPack.m_RTTexture->WorkSRViewAddress());

	// 通常テクスチャ用サンプラーのセット
	KdShaderManager::Instance().ChangeSamplerState(KdSamplerState::Anisotropic_Wrap, 0);

	// 影ぼかし用の比較機能付きサンプラーのセット
	KdShaderManager::Instance().ChangeSamplerState(KdSamplerState::Linear_Clamp_Cmp, 1);

	// デカール用テクスチャは Texture2DArray に詰め直してから 1 本の SRV として渡す。
	// シェーダー側は decalIndex をそのまま配列スライス番号として扱う。
	RefreshDecalTextureArray(m_decalTexArray, m_spDecalTex, m_cb4_Decal.Work().DecalNum);
	ID3D11ShaderResourceView* decalSRV = m_decalTexArray.WorkSRView();
	KdDirect3D::Instance().WorkDevContext()->PSSetShaderResources(13, 1, &decalSRV);
	m_cb4_Decal.Write();
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// 陰影ありオブジェクトの描画修了
// ===== ===== ===== ===== ===== ===== ===== ===== ===== ===== ===== =====
// 影を書き込む用に使用していたGenDepthFromLightで生成した深度SRVの解放
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdStandardShader::EndLit()
{
	ID3D11ShaderResourceView* pNullSRV = nullptr;
	KdDirect3D::Instance().WorkDevContext()->PSSetShaderResources(10, 1, &pNullSRV);
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// 陰影をつけないオブジェクトの描画の直前処理（エフェクトや半透明物）
// ===== ===== ===== ===== ===== ===== ===== ===== ===== ===== ===== =====
// シェーダーのパイプライン変更
// UnLitShaderで使用するリソースのバッファー設定
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdStandardShader::BeginUnLit()
{
	if (KdShaderManager::Instance().SetVertexShader(m_VS_UnLit))
	{
		KdShaderManager::Instance().SetInputLayout(m_inputLayout);

		KdShaderManager::Instance().SetVSConstantBuffer(0, m_cb0_Obj.GetAddress());
		KdShaderManager::Instance().SetVSConstantBuffer(1, m_cb1_Mesh.GetAddress());
	}

	if (KdShaderManager::Instance().SetPixelShader(m_PS_UnLit))
	{
		KdShaderManager::Instance().SetPSConstantBuffer(0, m_cb0_Obj.GetAddress());
		KdShaderManager::Instance().SetPSConstantBuffer(2, m_cb2_Material.GetAddress());
	}
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// 陰影なしオブジェクトの描画終了
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdStandardShader::EndUnLit()
{
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// 影を生み出すオブジェクトの情報描画（光を遮る物体）
// ===== ===== ===== ===== ===== ===== ===== ===== ===== ===== ===== =====
// シェーダーのパイプライン変更
// GenDepthMapFromLightShaderで使用するリソースのバッファー設定
// 書き込むテクスチャーを深度用の赤一色のテクスチャに切り替え
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdStandardShader::BeginGenerateDepthMapFromLight()
{
	if (KdShaderManager::Instance().SetVertexShader(m_VS_GenDepthFromLight))
	{
		KdShaderManager::Instance().SetInputLayout(m_inputLayout);

		KdShaderManager::Instance().SetVSConstantBuffer(0, m_cb0_Obj.GetAddress());
		KdShaderManager::Instance().SetVSConstantBuffer(1, m_cb1_Mesh.GetAddress());
	}

	// ボーン情報をセット(スキンメッシュ対応)
	KdShaderManager::Instance().SetVSConstantBuffer(3, m_cb3_Bone.GetAddress());

	if (KdShaderManager::Instance().SetPixelShader(m_PS_GenDepthFromLight))
	{
		KdShaderManager::Instance().SetPSConstantBuffer(0, m_cb0_Obj.GetAddress());
	}

	m_depthMapFromLightRTPack.ClearTexture(kRedColor);
	m_depthMapFromLightRTChanger.ChangeRenderTarget(m_depthMapFromLightRTPack);
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// 影を生み出すオブジェクトの描画終了
// ===== ===== ===== ===== ===== ===== ===== ===== ===== ===== ===== =====
// レンダーターゲットを元に戻す
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdStandardShader::EndGenerateDepthMapFromLight()
{
	m_depthMapFromLightRTChanger.UndoRenderTarget();
}


//================================================
// 描画関数
//================================================

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// メッシュを描画
// ===== ===== ===== ===== ===== ===== ===== ===== ===== ===== ===== =====
// メッシュの頂点データや3Dワールド情報・マテリアル情報をシェーダー(GPU)に転送する
// サブセットごとに描画命令を呼び出す：サブセットの個数分処理が重くなる
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdStandardShader::DrawMesh(const KdMesh* mesh, const Math::Matrix& mWorld,
	const std::vector<KdMaterial>& materials, const Math::Vector4& colRate, const Math::Vector3& emissive)
{
	if (mesh == nullptr) { return; }

	// メッシュの頂点情報転送
	mesh->SetToDevice();

	// 3Dワールド行列転送
	m_cb1_Mesh.Work().mW = mWorld;
	m_cb1_Mesh.Write();

	// 全サブセット
	for (UINT subi = 0; subi < mesh->GetSubsets().size(); subi++)
	{
		// 面が１枚も無い場合はスキップ
		if (mesh->GetSubsets()[subi].FaceCount == 0)continue;

		// マテリアルデータの転送
		const KdMaterial& material = materials[mesh->GetSubsets()[subi].MaterialNo];
		WriteMaterial(material, colRate, emissive);

		//-----------------------
		// サブセット描画
		//-----------------------
		mesh->DrawSubset(subi);
	}
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// モデルデータを描画（スタティック(アニメーションをしない)なモデル専用
// ===== ===== ===== ===== ===== ===== ===== ===== ===== ===== ===== =====
// データに所属する全ての描画用メッシュを描画する
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdStandardShader::DrawModel(const KdModelData& rModel, const Math::Matrix& mWorld,
	const Math::Color& colRate, const Math::Vector3& emissive)
{
	// オブジェクト単位の情報転送
	if (m_dirtyCBObj)
	{
		m_cb0_Obj.Write();
	}

	auto& dataNodes = rModel.GetOriginalNodes();

	// 全描画用メッシュノードを描画
	for (auto& nodeIdx : rModel.GetDrawMeshNodeIndices())
	{
		// 描画
		DrawMesh(dataNodes[nodeIdx].m_spMesh.get(), dataNodes[nodeIdx].m_worldTransform * mWorld, 
			rModel.GetMaterials(), colRate, emissive);
	}

	// 定数に変更があった場合は自動的に初期状態に戻す
	if(m_dirtyCBObj)
	{
		ResetCBObject();
	}
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// モデルワークを描画（ダイナミック(アニメーションをする)なモデルに対応
// ===== ===== ===== ===== ===== ===== ===== ===== ===== ===== ===== =====
// データに所属する全ての描画用メッシュをワークの3D行列に従って描画する
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdStandardShader::DrawModel(KdModelWork& rModel, const Math::Matrix& mWorld,
	const Math::Color& colRate, const Math::Vector3& emissive)
{
	if (!rModel.IsEnable()) { return; }

	const std::shared_ptr<KdModelData>& data = rModel.GetData();

	// データがないときはスキップ
	if (data == nullptr) { return; }

	if (rModel.NeedCalcNodeMatrices())
	{
		rModel.CalcNodeMatrices();
	}

	// オブジェクト単位の情報転送(スキンメッシュ対応)
	SetIsSkinMeshObj(data->IsSkinMesh());
	if (m_dirtyCBObj)
	{
		m_cb0_Obj.Write();
	}

	auto& workNodes = rModel.GetNodes();
	auto& dataNodes = data->GetOriginalNodes();

	// スキンメッシュモデルの場合：ボーン情報を書き込み(スキンメッシュ対応)
	if (data->IsSkinMesh())
	{
		// ノード内からボーン情報を取得
		for (auto&& nodeIdx : data->GetBoneNodeIndices())
		{
			if (nodeIdx >= KdStandardShader::maxBoneBufferSize) { assert(0 && "転送できるボーンの上限数を超えました"); return; }

			auto& dataNode = dataNodes[nodeIdx];
			auto& workNode = workNodes[nodeIdx];

			// ボーン情報からGPUに渡す行列の計算
			m_cb3_Bone.Work().mBones[dataNode.m_boneIndex] = dataNode.m_boneInverseWorldMatrix * workNode.m_worldTransform;

			m_cb3_Bone.Write();
		}
	}
	

	// 全描画用メッシュノードを描画
	for (auto& nodeIdx : data->GetDrawMeshNodeIndices())
	{
		// 描画
		DrawMesh(dataNodes[nodeIdx].m_spMesh.get(), workNodes[nodeIdx].m_worldTransform * mWorld,
			data->GetMaterials(), colRate, emissive);
	}

	// 定数に変更があった場合は自動的に初期状態に戻す
	if (m_dirtyCBObj)
	{
		ResetCBObject();
	}
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// ポリゴンを描画（モデル以外のプログラム上で生成された頂点の集合体
// ===== ===== ===== ===== ===== ===== ===== ===== ===== ===== ===== =====
// データに所属する全ての描画用メッシュをワークの3D行列に従って描画する
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdStandardShader::DrawPolygon(const KdPolygon& rPolygon, const Math::Matrix& mWorld,
	const Math::Color& colRate, const Math::Vector3& emissive)
{
	if (!rPolygon.IsEnable()) { return; }

	// ポリゴン描画用の頂点取得
	auto& vertices = rPolygon.GetVertices();

	// 頂点数が3より少なければポリゴンが形成できないので描画不能
	if (vertices.size() < 3) { return; }

	// オブジェクト単位の定数バッファで変更があった場合のみ情報転送
	if (m_dirtyCBObj)
	{
		m_cb0_Obj.Write();
	}

	// 3Dワールド行列転送
	m_cb1_Mesh.Work().mW = mWorld;
	m_cb1_Mesh.Write();

	// マテリアルの転送
	if (rPolygon.GetMaterial())
	{
		WriteMaterial(*rPolygon.GetMaterial(), colRate, emissive);
	}
	else
	{
		WriteMaterial(KdMaterial(), colRate, emissive);
	}

	KdShaderManager::Instance().ChangeRasterizerState(KdRasterizerState::CullNone);

	// サンプラーステートの変更:ポリゴンの描画なので、テクスチャの末端が繰り返されると不自然な描画になるため変更が必要
	if (KdShaderManager::Instance().IsPixelArtStyle())
	{
		KdShaderManager::Instance().ChangeSamplerState(KdSamplerState::Point_Clamp);
	}
	else
	{
		KdShaderManager::Instance().ChangeSamplerState(KdSamplerState::Anisotropic_Clamp);
	}

	// 描画パイプラインのチェック
	ID3D11VertexShader* pNowVS = nullptr;
	KdDirect3D::Instance().WorkDevContext()->VSGetShader(&pNowVS, nullptr, nullptr);
	bool isLitShader = m_VS_Lit == pNowVS;
	KdSafeRelease(pNowVS);

	// 陰影ありのシェーダーで2Dオブジェクトを描画する時
	if (isLitShader && rPolygon.Is2DObject())
	{
		std::vector<KdPolygon::Vertex> _2DVertices = vertices;

		// ポリゴンの法線を光に向ける処理：どの方向に向いていても光の影響を正面からに受けるように変換
		ConvertNormalsFor2D(_2DVertices, mWorld);

		// 2DObject用に変換した頂点配列を描画
		KdDirect3D::Instance().DrawVertices(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, (signed)_2DVertices.size(), &_2DVertices[0], sizeof(KdPolygon::Vertex));
	}
	else
	{
		// 頂点配列を描画
		KdDirect3D::Instance().DrawVertices(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, (signed)vertices.size(), &vertices[0], sizeof(KdPolygon::Vertex));
	}

	KdShaderManager::Instance().UndoSamplerState();

	KdShaderManager::Instance().UndoRasterizerState();

	// 定数に変更があった場合は自動的に初期状態に戻す
	if (m_dirtyCBObj)
	{
		ResetCBObject();
	}
}

void KdStandardShader::DrawVertices(const std::vector<KdPolygon::Vertex>& vertices, const Math::Matrix& mWorld,
	const Math::Color& colRate)
{
	// 頂点数が2より少なければポリゴンが形成できないので描画不能
	if (vertices.size() < 2) { return; }

	// オブジェクト単位の定数バッファで変更があった場合のみ情報転送
	if (m_dirtyCBObj)
	{
		m_cb0_Obj.Write();
	}

	// 3Dワールド行列転送
	m_cb1_Mesh.Work().mW = mWorld;
	m_cb1_Mesh.Write();

	// マテリアルの転送
	WriteMaterial(KdMaterial(), colRate, Math::Vector3::Zero);

	KdShaderManager::Instance().ChangeRasterizerState(KdRasterizerState::CullNone);
	KdShaderManager::Instance().ChangeDepthStencilState(KdDepthStencilState::ZDisable);

	// サンプラーステートの変更:ポリゴンの描画なので、テクスチャの末端が繰り返されると不自然な描画になるため変更が必要
	if (KdShaderManager::Instance().IsPixelArtStyle())
	{
		KdShaderManager::Instance().ChangeSamplerState(KdSamplerState::Point_Clamp);
	}
	else
	{
		KdShaderManager::Instance().ChangeSamplerState(KdSamplerState::Anisotropic_Clamp);
	}

	// 描画パイプラインのチェック
	ID3D11VertexShader* pNowVS = nullptr;
	KdDirect3D::Instance().WorkDevContext()->VSGetShader(&pNowVS, nullptr, nullptr);

	KdSafeRelease(pNowVS);

	// 頂点配列を描画
	KdDirect3D::Instance().DrawVertices(D3D_PRIMITIVE_TOPOLOGY_LINELIST, (signed)vertices.size(), &vertices[0], sizeof(KdPolygon::Vertex));

	KdShaderManager::Instance().UndoSamplerState();

	KdShaderManager::Instance().UndoDepthStencilState();

	KdShaderManager::Instance().UndoRasterizerState();
	// 定数に変更があった場合は自動的に初期状態に戻す
	if (m_dirtyCBObj)
	{
		ResetCBObject();
	}
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// KdShaderManagerの初期化時に呼び出される
// ===== ===== ===== ===== ===== ===== ===== ===== ===== ===== ===== =====
// シェーダー本体の生成
// シェーダーで利用する定数バッファの生成
// 影用の光からの深度情報テクスチャを生成
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdStandardShader::Init()
{
	//-------------------------------------
	// 頂点シェーダ(スキンメッシュ対応)
	//-------------------------------------
	{
		// コンパイル済みのシェーダーヘッダーファイルをインクルード
#include "KdStandardShader_VS_Lit.shaderInc"

		// 頂点シェーダー作成
		if (FAILED(KdDirect3D::Instance().WorkDev()->CreateVertexShader(compiledBuffer, sizeof(compiledBuffer), nullptr, &m_VS_Lit))) {
			assert(0 && "頂点シェーダー作成失敗");
			Release();
			return false;
		}

		// １頂点の詳細な情報
		std::vector<D3D11_INPUT_ELEMENT_DESC> layout = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,		0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,			0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,		0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,		0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT,		0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "SKININDEX",	0, DXGI_FORMAT_R16G16B16A16_UINT,	0, 48,	D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "SKINWEIGHT",	0, DXGI_FORMAT_R32G32B32A32_FLOAT,	0, 56,	D3D11_INPUT_PER_VERTEX_DATA, 0 }
		};

		// 頂点入力レイアウト作成
		if (FAILED(KdDirect3D::Instance().WorkDev()->CreateInputLayout(
			&layout[0],				// 入力エレメント先頭アドレス
			(UINT)layout.size(),	// 入力エレメント数
			&compiledBuffer[0],		// 頂点バッファのバイナリデータ
			sizeof(compiledBuffer),	// 上記のバッファサイズ
			&m_inputLayout))
			) {
			assert(0 && "CreateInputLayout失敗");
			Release();
			return false;
		}
	}

	{
#include "KdStandardShader_VS_GenDepthMapFromLight.shaderInc"

		// 頂点シェーダー作成
		if (FAILED(KdDirect3D::Instance().WorkDev()->CreateVertexShader(compiledBuffer, sizeof(compiledBuffer), nullptr, &m_VS_GenDepthFromLight))) {
			assert(0 && "頂点シェーダー作成失敗");
			Release();
			return false;
		}
	}

	{
#include "KdStandardShader_VS_UnLit.shaderInc"

		// 頂点シェーダー作成
		if (FAILED(KdDirect3D::Instance().WorkDev()->CreateVertexShader(compiledBuffer, sizeof(compiledBuffer), nullptr, &m_VS_UnLit))) {
			assert(0 && "頂点シェーダー作成失敗");
			Release();
			return false;
		}
	}

	//-------------------------------------
	// ピクセルシェーダ
	//-------------------------------------
	{
#include "KdStandardShader_PS_Lit.shaderInc"

		if (FAILED(KdDirect3D::Instance().WorkDev()->CreatePixelShader(compiledBuffer, sizeof(compiledBuffer), nullptr, &m_PS_Lit))) {
			assert(0 && "ピクセルシェーダー作成失敗");
			Release();
			return false;
		}
	}

	{
#include "KdStandardShader_PS_GenDepthMapFromLight.shaderInc"

		if (FAILED(KdDirect3D::Instance().WorkDev()->CreatePixelShader(compiledBuffer, sizeof(compiledBuffer), nullptr, &m_PS_GenDepthFromLight))) {
			assert(0 && "ピクセルシェーダー作成失敗");
			Release();
			return false;
		}
	} 
	
	{
#include "KdStandardShader_PS_UnLit.shaderInc"

		if (FAILED(KdDirect3D::Instance().WorkDev()->CreatePixelShader(compiledBuffer, sizeof(compiledBuffer), nullptr, &m_PS_UnLit))) {
			assert(0 && "ピクセルシェーダー作成失敗");
			Release();
			return false;
		}
	}
	//-------------------------------------
	// 定数バッファ作成
	//-------------------------------------
	m_cb0_Obj.Create();
	m_cb1_Mesh.Create();
	m_cb2_Material.Create();
	m_cb3_Bone.Create();
	m_cb4_Decal.Create();
	ClearDecals();

	std::shared_ptr<KdTexture> ds = std::make_shared<KdTexture>();
	ds->CreateDepthStencil(1024, 1024);
	D3D11_VIEWPORT vp = {
		0.0f, 0.0f,
		static_cast<float>(ds->GetWidth()),
		static_cast<float>(ds->GetHeight()),
		0.0f, 1.0f };

	m_depthMapFromLightRTPack.CreateRenderTarget(1024, 1024, true, DXGI_FORMAT_R32_FLOAT);
	m_depthMapFromLightRTPack.ClearTexture(kRedColor);

	SetDissolveTexture(*KdAssets::Instance().m_textures.GetData("Asset/Textures/System/WhiteNoise.png"));


	return true;
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// シェーダー本体の解放
// 利用していたコンスタントバッファの解放
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdStandardShader::Release()
{
	KdSafeRelease(m_VS_Lit);
	KdSafeRelease(m_VS_GenDepthFromLight);
	KdSafeRelease(m_VS_UnLit);

	KdSafeRelease(m_inputLayout);
	
	KdSafeRelease(m_PS_Lit);
	KdSafeRelease(m_PS_GenDepthFromLight);
	KdSafeRelease(m_PS_UnLit);

	m_cb0_Obj.Release();
	m_cb1_Mesh.Release();
	m_cb2_Material.Release();
	// スキンメッシュ対応
	m_cb3_Bone.Release();
	m_cb4_Decal.Release();
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// 描画用マテリアル情報の転送
// ===== ===== ===== ===== ===== ===== ===== ===== ===== ===== ===== =====
// それぞれのマテリアルの影響倍率値とテクスチャを設定
// BaseColor：基本色 / Emissive：自己発光色 / Metalic：金属性(テカテカ) / Roughness：粗さ(材質の色の反映度)
// テクスチャは法線マップ以外は未設定なら白1ピクセルのシステムテクスチャを指定
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdStandardShader::WriteMaterial(const KdMaterial& material, const Math::Vector4& colRate, const Math::Vector3& emiRate)
{
	//-----------------------
	// マテリアル情報を定数バッファへ書き込む
	//-----------------------
	m_cb2_Material.Work().BaseColor = material.m_baseColorRate * colRate;
	m_cb2_Material.Work().Emissive = material.m_emissiveRate * emiRate;
	m_cb2_Material.Work().Metallic = material.m_metallicRate;
	m_cb2_Material.Work().Roughness = material.m_roughnessRate;
	m_cb2_Material.Write();

	//-----------------------
	// テクスチャセット
	//-----------------------
	ID3D11ShaderResourceView* srvs[4];

	srvs[0] = material.m_baseColorTex ? material.m_baseColorTex->WorkSRView() : KdDirect3D::Instance().GetWhiteTex()->WorkSRView();
	srvs[1] = material.m_metallicRoughnessTex ? material.m_metallicRoughnessTex->WorkSRView() : KdDirect3D::Instance().GetWhiteTex()->WorkSRView();
	srvs[2] = material.m_emissiveTex ? material.m_emissiveTex->WorkSRView() : KdDirect3D::Instance().GetWhiteTex()->WorkSRView();
	srvs[3] = material.m_normalTex ? material.m_normalTex->WorkSRView() : KdDirect3D::Instance().GetNormalTex()->WorkSRView();

	// セット
	KdDirect3D::Instance().WorkDevContext()->PSSetShaderResources(0, _countof(srvs), srvs);
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// ポリゴンがどの方向に向いていても光の影響を正面からに受けるように頂点の法線を変換
// 2Dキャラクタを描画する時などは必要
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdStandardShader::ConvertNormalsFor2D(std::vector<KdPolygon::Vertex>& target, const Math::Matrix& mWorld)
{
	// 平行光の向き
	const Math::Vector3& dirLight_Dir = KdShaderManager::Instance().GetLightCB().DirLight_Dir;

	// どの角度を向いていても表面は常に光の方向を向いている状態：横向きの板ポリが暗くならない対策
	Math::Vector3 normal = Math::Vector3::TransformNormal(-dirLight_Dir, mWorld.Invert());
	Math::Vector3 tangent = (normal != Math::Vector3::Up) ?
		normal.Cross(Math::Vector3::Up) : normal.Cross(Math::Vector3::Right);

	for (size_t i = 0; i < target.size(); ++i)
	{
		target[i].normal = normal;
		target[i].tangent = tangent;
	}
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// オブジェクト定数バッファを初期状態に戻す
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdStandardShader::ResetCBObject()
{
	m_cb0_Obj.Work() = cbObject();

	m_cb0_Obj.Write();

	m_dirtyCBObj = false;
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// デカールオブジェクト
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdStandardShader::ClearDecals()
{
	// デカールは毎フレーム PreDraw から再登録されるため、描画開始時に前フレーム分を空に戻す。
	// 行列・色・法線条件だけでなく、Texture2DArray を組み立てる元テクスチャ情報もここで初期化する。
	m_cb4_Decal.Work() = cbDecal();
	for (auto& spDecalTex : m_spDecalTex)
	{
		spDecalTex = nullptr;
	}
	m_decalTexArray.Release();
}

void KdStandardShader::AddDecal(const Math::Matrix& decalMatrix, const std::shared_ptr<KdTexture>& spTexture,
	const Math::Color& color, float normalThreshold)
{
	auto& decalInfo = m_cb4_Decal.Work();
	if (decalInfo.DecalNum >= maxDecalNum) { return; }

	const int decalIndex = decalInfo.DecalNum;
	// デカールのローカル上方向をワールド空間へ変換し、
	// 「どの向きの面にだけ貼るか」を表す基準法線として使う。
	Math::Vector3 normal = Math::Vector3::TransformNormal(Math::Vector3::Up, decalMatrix);
	if (normal.LengthSquared() <= 0.0f) { return; }
	normal.Normalize();

	// シェーダー側では各ピクセルのワールド座標をデカールのローカル空間へ戻して、
	// 範囲判定や UV 計算をするため、ここでは逆行列を保持しておく。
	decalInfo.WorldToDecal[decalIndex] = decalMatrix.Invert();
	decalInfo.Color[decalIndex] = color;
	// xyz は貼り付け対象面の基準法線、w は surfaceNormal との内積しきい値。
	// w が 1 に近いほど、デカール正面に近い面だけに限定される。
	decalInfo.NormalThreshold[decalIndex] = { normal.x, normal.y, normal.z, std::clamp(normalThreshold, 0.0f, 1.0f) };

	// 各デカールは自分用の元テクスチャを保持しておき、
	// BeginLit 時に decalIndex 順で Texture2DArray の各スライスへ詰め直す。
	// テクスチャ未指定時でも必ず配列化できるよう、白テクスチャを保険として保持しておく。
	m_spDecalTex[decalIndex] = spTexture ? spTexture : KdDirect3D::Instance().GetWhiteTex();

	decalInfo.DecalNum++;
}
