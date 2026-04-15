#include "SkinmeshMan.h"
#include "../../../Camera/CameraBase.h"

void SkinmeshMan::Init()
{
	// 親クラスのInit()を呼び出し
	// ↓中でやってることは各種変数の初期化
	CharacterBase::Init();

	if (!m_spModel)
	{
		// スキンメッシュメン
		m_spModel = std::make_shared<KdModelWork>();
		m_spModel->SetModelData("Asset/Models/Character/SkinMeshMan/SkinMeshMan.gltf");
		m_spAnimator = std::make_shared<KdAnimator>();
		m_spAnimator->SetAnimation(m_spModel->GetAnimation("Walk"));

		// 当たり判定用の旧情報を生成
		m_spSphere = std::make_shared<DirectX::BoundingSphere>();
		m_spSphere->Center = GetPos() + Math::Vector3(0, 1.0f, 0);
		m_spSphere->Radius = 0.5f;

		m_IsPlayer = true;
	}
}

void SkinmeshMan::Update()
{
	// キャラクターには常に重力がかかる
	m_Gravity += 0.01f;
	m_mWorld._42 -= m_Gravity;

	// キャラクターの移動速度(真似しちゃダメですよ)
	float			_moveSpd	= 0.05f;
	m_worldPos					= GetPos();

	Math::Vector3 _moveVec = Math::Vector3::Zero;
	if (GetAsyncKeyState('D')) { _moveVec.x = 1.0f; }
	if (GetAsyncKeyState('A')) { _moveVec.x = -1.0f; }
	if (GetAsyncKeyState('W')) { _moveVec.z = 1.0f; }
	if (GetAsyncKeyState('S')) { _moveVec.z = -1.0f; }
	if (GetAsyncKeyState(VK_SPACE)) { m_Gravity = -0.5; }

	const std::shared_ptr<CameraBase> _spCamera = GetCamera().lock();
	if (_spCamera)
	{
		_moveVec = _moveVec.TransformNormal(_moveVec, _spCamera->GetRotationYMatrix());
	}
	_moveVec.Normalize();
	_moveVec *= _moveSpd;
	m_worldPos += _moveVec;

	// キャラクターの回転行列を創る
	UpdateRotate(_moveVec);

	// 親クラスのUpdate()を呼び出し
	// ↓中でやってることは行列の更新処理
	CharacterBase::Update();

	if (GetAsyncKeyState(VK_SPACE)) 
	{
		KdDebugGUI::Instance().ClearLog();
	}
}

void SkinmeshMan::DrawSprite()
{
	// フォント描画テスト
//	KdShaderManager::Instance().m_spriteShader.SetMatrix(Math::Matrix::CreateScale(1));
//	KdShaderManager::Instance().m_spriteShader.DrawFont(Math::Vector2(0,0), &kWhiteColor, "もとおり、どうしてそうまでして\nいきようとするの？");
//	KdShaderManager::Instance().m_spriteShader.SetMatrix(Math::Matrix::CreateScale(1));
}

void SkinmeshMan::PreDraw()
{
	m_pDebugWire->AddDebugCapsule(GetMatrix(), 0.3f, 1.5f, { 0,0.8f,0 });
//	m_pDebugWire->AddDebugSphere(m_spSphere->Center, 0.5f);
}