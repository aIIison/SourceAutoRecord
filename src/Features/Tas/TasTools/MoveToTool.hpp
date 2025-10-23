#pragma once

#include "../TasTool.hpp"
#include "Utils/SDK/Math.hpp"

struct MoveToParams : public TasToolParams {
	MoveToParams()
		: TasToolParams(false) {}

	MoveToParams(Vector point)
		: TasToolParams(true)
		, point(point) {}

	Vector point;
};

class MoveToTool : public TasToolWithParams<MoveToParams> {
public:
	MoveToTool(int slot)
		: TasToolWithParams("moveto", slot) {}

	virtual std::shared_ptr<TasToolParams> ParseParams(std::vector<std::string>);
	virtual void Apply(TasFramebulk &bulk, const TasPlayerInfo &pInfo);
};
