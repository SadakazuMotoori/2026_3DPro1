#pragma once
//============================================================
//
// 基本シェーダー
//
//============================================================
class KdStandardShader
{
public:
	// 歪み描画の種類
	// 歪み専用ピクセルシェーダー側では、この値を見て描き方を切り替える
	enum class DistortionType
	{
		None = 0,		// 歪みなし
		HeatHaze = 1,	// 蜃気楼・熱気のゆらぎ
		ShockWave = 2,	// 円形の衝撃波
	};

	// スキンメッシュ対応
	static const int maxBoneBufferSize = 300;

	// 定数バッファ(オブジェクト単位更新)
	struct cbObject
	{
		// UV操作
		Math::Vector2	UVOffset = { 0.0f, 0.0f };
		Math::Vector2	UVTiling = { 1.0f, 1.0f };

		// フォグ有効
		int				FogEnable = 1;

		// エミッシブのみの描画
		int				OnlyEmissie = 0;

		// スキンメッシュオブジェクトかどうか(スキンメッシュ対応)
		int				IsSkinMeshObj = 0;

		// ディゾルブ関連
		float			DissolveThreshold = 0.0f;	// 0 ～ 1
		float			DissolveEdgeRange = 0.03f;	// 0 ～ 1

		Math::Vector3	DissolveEmissive = { 0.0f, 1.0f, 1.0f };
	};

	// 定数バッファ(歪み描画専用)
	// 既存のオブジェクト定数バッファへ影響を出したくないため、
	// 歪み用のパラメータは専用バッファへ分離して管理する
	struct cbDistortion
	{
		int				Type = 0;					// 歪み描画の種類
		float			Strength = 0.0f;			// 最終合成で使う UV ずらし量
		float			Time = 0.0f;				// 歪みアニメーション時間
		float			_blank0 = 0.0f;

		// Param0 と Param1 は、歪みの種類ごとに用途を切り替える
		// HeatHaze ではノイズのタイル数やスクロール速度、
		// ShockWave では半径や輪の太さを入れる
		Math::Vector4	Param0 = { 0.0f, 0.0f, 1.0f, 1.0f };
		Math::Vector4	Param1 = { 0.0f, 0.0f, 0.0f, 0.0f };
	};

	// 定数バッファ(メッシュ単位更新)
	struct cbMesh
	{
		Math::Matrix	mW;
	};

	// 定数バッファ(マテリアル単位更新)
	struct cbMaterial
	{
		Math::Vector4	BaseColor = { 1.0f, 1.0f, 1.0f, 1.0f };

		Math::Vector3	Emissive = { 1.0f, 1.0f, 1.0f };
		float			Metallic = 0.0f;

		float			Roughness = 1.0f;
		float			_blank[3] = { 0.0f, 0.0f ,0.0f };
	};

	// 定数バッファ(ボーン単位更新：スキンメッシュ対応)
	struct cbBone {
		Math::Matrix mBones[300];
	};

	//================================================
	// 設定・取得
	//================================================

	// UVタイリング設定
	void SetUVTiling(const Math::Vector2& tiling)
	{
		m_cb0_Obj.Work().UVTiling = tiling;

		m_dirtyCBObj = true;
	}

	// UVオフセット設定
	void SetUVOffset(const Math::Vector2& offset)
	{
		m_cb0_Obj.Work().UVOffset = offset;

		m_dirtyCBObj = true;
	}

	// フォグ有効/無効
	void SetFogEnable(bool enable)
	{
		m_cb0_Obj.Work().FogEnable = enable;

		m_dirtyCBObj = true;
	}

	// ディゾルブ設定
	void SetDissolve(float threshold, const float* range = nullptr, const Math::Vector3* edgeColor = nullptr)
	{
		auto& cbObj = m_cb0_Obj.Work();

		cbObj.DissolveThreshold = threshold;

		if (range)
		{
			cbObj.DissolveEdgeRange = *range;
		}

		if (edgeColor)
		{
			cbObj.DissolveEmissive = *edgeColor;
		}

		m_dirtyCBObj = true;
	}

	// 歪み描画タイプの設定
	// どの表現用の分岐を使うかを、歪み用定数バッファへ記録する
	void SetDistortionType(DistortionType type)
	{
		m_cb4_Distortion.Work().Type = static_cast<int>(type);

		m_dirtyCBDistortion = true;
	}

	// 歪み強度の設定
	// 値が大きいほど、最終合成時の背景の曲がり量が強くなる
	void SetDistortionStrength(float strength)
	{
		m_cb4_Distortion.Work().Strength = strength;

		m_dirtyCBDistortion = true;
	}

	// 歪み用の時間パラメータ設定
	// ノイズスクロールやループ演出の進行用
	void SetDistortionTime(float time)
	{
		m_cb4_Distortion.Work().Time = time;

		m_dirtyCBDistortion = true;
	}

	// 歪み用の汎用パラメータ設定
	// 種類ごとに意味が変わるので、呼び出し側で用途を合わせて渡す
	void SetDistortionParam0(const Math::Vector4& param)
	{
		m_cb4_Distortion.Work().Param0 = param;

		m_dirtyCBDistortion = true;
	}

	// 歪み用の汎用パラメータ設定
	// 種類ごとに意味が変わるので、呼び出し側で用途を合わせて渡す
	void SetDistortionParam1(const Math::Vector4& param)
	{
		m_cb4_Distortion.Work().Param1 = param;

		m_dirtyCBDistortion = true;
	}

	// ディゾルブテクスチャ設定
	void SetDissolveTexture(KdTexture& dissolveMask)
	{
		KdDirect3D::Instance().WorkDevContext()->PSSetShaderResources(11, 1, dissolveMask.WorkSRViewAddress());
	}

	// デフォルトディゾルブテクスチャ設定
	void SetDefaultDissolveTexture(std::shared_ptr<KdTexture>& spDissolveMask)
	{
		if (!spDissolveMask) { return; }

		m_dissolveTex = spDissolveMask;

		SetDissolveTexture(*spDissolveMask);
	}

	// デフォルトのディゾルブテクスチャに戻す
	void ResetDissolveTexture()
	{
		if (!m_dissolveTex) { return; }

		SetDissolveTexture(*m_dissolveTex);
	}

	//================================================
	// 各定数バッファの取得
	//================================================
	const cbObject& GetObjectCB() const { return m_cb0_Obj.Get(); }

	const cbMesh& MeshCB() const { return m_cb1_Mesh.Get(); }

	const cbMaterial& WorkMaterialCB() const { return m_cb2_Material.Get(); }

	// スキンメッシュ対応
	const cbBone& WorkBoneCB() const { return m_cb3_Bone.Get(); }

	//================================================
	// 描画準備
	//================================================
	// 陰影をつけるオブジェクト等を描画する前後に行う
	void BeginLit();
	void EndLit();

	// 陰影をつけないオブジェクト等を描画する前後に行う
	void BeginUnLit();
	void EndUnLit();

	// 歪み用オブジェクトを描画する前後に行う
	// ここでは色を描くのではなく、最終合成で使う歪みベクトルを描く
	// BeginDistortion 中は StandardShader の出力先が
	// 「通常カラー」から「歪み情報」へ切り替わる
	void BeginDistortion();
	void EndDistortion();

	// 最も初めに行う、光を遮るオブジェクトを描画する前後に行う
	void BeginGenerateDepthMapFromLight();
	void EndGenerateDepthMapFromLight();

	//================================================
	// 描画関数
	//================================================
	// メッシュ描画
	void DrawMesh(const KdMesh* mesh, const Math::Matrix& mWorld, const std::vector<KdMaterial>& materials,
		const Math::Vector4& col, const Math::Vector3& emissive);

	// モデルデータ描画：アニメーションに非対応
	void DrawModel(const KdModelData& rModel, const Math::Matrix& mWorld = Math::Matrix::Identity, 
		const Math::Color& colRate = kWhiteColor, const Math::Vector3& emissive = Math::Vector3::Zero);

	// モデルワーク描画：アニメーションに対応
	void DrawModel(KdModelWork& rModel, const Math::Matrix& mWorld = Math::Matrix::Identity,
		const Math::Color& colRate = kWhiteColor, const Math::Vector3& emissive = Math::Vector3::Zero);

	// 任意の頂点群からなるポリゴン描画
	void DrawPolygon(const KdPolygon& poly, const Math::Matrix& mWorld = Math::Matrix::Identity,
		const Math::Color& colRate = kWhiteColor, const Math::Vector3& emissive = Math::Vector3::Zero);

	// 任意の頂点群からなるポリゴンライン描画
	void DrawVertices(const std::vector<KdPolygon::Vertex>& vertices, const Math::Matrix& mWorld = Math::Matrix::Identity,
		const Math::Color& colRate = kWhiteColor);

	//================================================
	// 初期化・解放
	//================================================

	// 初期化
	bool Init();
	// 解放
	void Release();

	~KdStandardShader()
	{
		Release();
	}

	std::shared_ptr<KdTexture>& GetDepthTex() { return m_depthMapFromLightRTPack.m_RTTexture; }

private:

	// マテリアルのセット
	void WriteMaterial(const KdMaterial& material, const Math::Vector4& colRate, const Math::Vector3& emiRate);

	// ポリゴンの法線情報を2Dように書き換える
	void ConvertNormalsFor2D(std::vector<KdPolygon::Vertex>& target, const Math::Matrix& mWorld);

	// 定数バッファを初期状態に戻す
	void ResetCBObject();

	// スキンメッシュ有効かどうか(スキンメッシュ対応)
	void SetIsSkinMeshObj(bool isSkinMEshObj)
	{
		if (m_cb0_Obj.Work().IsSkinMeshObj != (int)isSkinMEshObj)
		{
			m_cb0_Obj.Work().IsSkinMeshObj = isSkinMEshObj;
			m_dirtyCBObj = true;
		}
	}

	// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
	// Lit：陰影をつけるオブジェクトの描画用（不透明な物体やキャラクタの板ポリなど
	// 平行光・点光源などの影響を受け角度によって色を変化させるオブジェクトを描画するシェーダー
	// ===== ===== ===== ===== ===== ===== ===== ===== ===== ===== ===== =====
	// UnLit：陰影のつかないオブジェクトの描画用（エフェクトや透明な物体など
	// 光の計算を行わずマテリアルの色をそのまま出力するシェーダー
	// ===== ===== ===== ===== ===== ===== ===== ===== ===== ===== ===== =====
	// GenDepthFromLight：光から見たオブジェクトの距離を赤で出力用
	// Litシェーダーで影の描画を行うために必要な情報テクスチャを作成するシェーダー
	// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////

	// 頂点シェーダー
	ID3D11VertexShader* m_VS_Lit = nullptr;					// 陰影あり
	ID3D11VertexShader* m_VS_UnLit = nullptr;				// 陰影なし
	ID3D11VertexShader* m_VS_GenDepthFromLight = nullptr;	// 光からの深度

	// 頂点入力レイアウト
	ID3D11InputLayout* m_inputLayout = nullptr;
	
	// ピクセルシェーダー
	ID3D11PixelShader* m_PS_Lit = nullptr;					// 陰影あり
	ID3D11PixelShader* m_PS_UnLit = nullptr;				// 陰影なし
	ID3D11PixelShader* m_PS_Distortion = nullptr;			// 歪み描画
	ID3D11PixelShader* m_PS_GenDepthFromLight = nullptr;	// 光からの深度

	// テクスチャ
	std::shared_ptr<KdTexture>	m_dissolveTex = nullptr;	// ディゾルブで使用するデフォルトテクスチャ

	// 定数バッファ
	KdConstantBuffer<cbObject>		m_cb0_Obj;				// オブジェクト単位で更新
	KdConstantBuffer<cbMesh>		m_cb1_Mesh;				// メッシュ毎に更新
	KdConstantBuffer<cbMaterial>	m_cb2_Material;			// マテリアル毎に更新
	KdConstantBuffer<cbBone>		m_cb3_Bone;				// ボーン事に更新(スキンメッシュ対応「)
	KdConstantBuffer<cbDistortion>	m_cb4_Distortion;		// 歪み描画専用

	KdRenderTargetPack	m_depthMapFromLightRTPack;
	KdRenderTargetChanger m_depthMapFromLightRTChanger;

	bool		m_dirtyCBObj = false;						// 定数バッファのオブジェクトに変更があったかどうか
	bool		m_dirtyCBDistortion = false;				// 定数バッファの歪み情報に変更があったかどうか
};
