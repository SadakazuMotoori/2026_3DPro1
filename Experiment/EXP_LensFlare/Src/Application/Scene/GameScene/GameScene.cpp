#include "GameScene.h"
#include"../SceneManager.h"

#include "../../GameObject/Camera/FPSCamera/FPSCamera.h"
#include "../../GameObject/Camera/TPSCamera/TPSCamera.h"

#include "../../GameObject/Map/Stage01/Stage01.h"
#include "../../GameObject/Map/Stage01/Environment/LensFlare/LensFlare.h"

#include "../../GameObject/Character/Player/SkinMeshMan/SkinMeshMan.h"
#include "../../GameObject/Character/Enemy/Enemy01/Enemy01.h"

void GameScene::Event()
{
	if (GetAsyncKeyState('T') & 0x8000)
	{
		SceneManager::Instance().SetNextScene
		(
			SceneManager::SceneType::Title
		);
	}
}

void GameScene::Init()
{
	//===================================================================
	// ステージ初期化
	//===================================================================
	std::shared_ptr<Stage01> _stage = std::make_shared<Stage01>();
	_stage->Init();
	AddObject(_stage);

	std::shared_ptr<LensFlare> _lensFlare = std::make_shared<LensFlare>();
	_lensFlare->Init();
	AddObject(_lensFlare);

	//===================================================================
	// キャラクター初期化
	//===================================================================
	std::shared_ptr<SkinmeshMan> _skinmeshMan = std::make_shared<SkinmeshMan>();
	_skinmeshMan->Init();
	_skinmeshMan->RegistHitObject(_stage);
	AddObject(_skinmeshMan);

	std::shared_ptr<Enemy01> _enemy01 = std::make_shared<Enemy01>();
	_enemy01->Init();
	_enemy01->RegistHitObject(_stage);
	AddObject(_enemy01);
	_skinmeshMan->RegistHitObject(_enemy01);

	//===================================================================
	// カメラ初期化
	//===================================================================
	std::shared_ptr<TPSCamera> _camera = std::make_shared<TPSCamera>();
	_camera->Init();
	_camera->SetTarget(_skinmeshMan);
	_camera->RegistHitObject(_stage);

	// レンズフレアにカメラ情報をセット
	_lensFlare->SetCamera(_camera);
	// キャラクターにカメラ情報をセット
	_skinmeshMan->SetCamera(_camera);
	AddObject(_camera);
}
