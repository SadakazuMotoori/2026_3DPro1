#pragma once

class MapBase : public KdGameObject
{
// [public]どっからでも使える(呼び出せる)！
public:
	MapBase() {}
	~MapBase()			override {}

	void Init()			override;
	void Update()		override;
	void DrawLit()		override;

// [private]「このクラス内」でしか使っちゃダメ！
private:

// [protected] このクラスを「継承している先でも」使っていいよ！
protected:
	std::shared_ptr<KdModelData> m_spModel = nullptr;
};
