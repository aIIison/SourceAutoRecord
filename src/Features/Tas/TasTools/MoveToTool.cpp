#include "MoveToTool.hpp"

#include "Features/Tas/TasParser.hpp"
#include "Modules/Console.hpp"
#include "Modules/Server.hpp"
#include "StrafeTool.hpp"

MoveToTool moveToTool[2] = {{0}, {1}};

std::shared_ptr<TasToolParams> MoveToTool::ParseParams(std::vector<std::string> args) {
	if (args.size() != 1 && args.size() != 2) {
		throw TasParserException(Utils::ssprintf("Wrong argument count for tool %s: %d", this->GetName(), args.size()));
	}
	if (args[0] == "off") {
		return std::make_shared<TasToolParams>(false);
	}

	float x;
	float y;

	try {
		x = std::stof(args[0]);
	} catch (...) {
		throw TasParserException(Utils::ssprintf("Bad x value for tool %s: %s", this->GetName(), args[0].c_str()));
	}

	try {
		y = std::stof(args[1]);
	} catch (...) {
		throw TasParserException(Utils::ssprintf("Bad y value for tool %s: %s", this->GetName(), args[1].c_str()));
	}

	return std::make_shared<MoveToParams>(Vector{x, y});
}

void MoveToTool::Apply(TasFramebulk &bulk, const TasPlayerInfo &player) {
	if (!params.enabled) return;

	auto player_pos = player.position;
	player_pos.z = 0;

	Vector vec;
	vec = this->params.point - player_pos;

	// absmov
	auto angles = player.angles;
	angles.y -= bulk.viewAnalog.x;
	angles.x -= bulk.viewAnalog.y;

	float forward_coef;
	if (fabsf(angles.x) >= 30.0f && !player.willBeGrounded) {
		forward_coef = cosOld(DEG2RAD(angles.x));
	} else {
		forward_coef = 1.0f;
	}

	float x = vec.x;
	float y = vec.y / forward_coef;

	if (y > 1.0f) {
		// We can't actually move this fast. Scale the movement down so 'y'
		// is within the allowed range
		x /= y;
		y = 1.0f;
	}

	bulk.moveAnalog.x = x;
	bulk.moveAnalog.y = y;

	if (autoStrafeTool->GetVelocityAfterMove(player, x, y).Length2D() < 1) {
		params.enabled = false;
	}
}
