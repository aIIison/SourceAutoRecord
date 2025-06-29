#include "Cvars.hpp"

#include "Command.hpp"
#include "Game.hpp"
#include "Modules/Client.hpp"
#include "Modules/Console.hpp"
#include "Modules/Engine.hpp"
#include "Modules/Server.hpp"
#include "Modules/Tier1.hpp"
#include "Offsets.hpp"
#include "SAR.hpp"
#include "Utils/json11.hpp"
#include "Utils/Memory.hpp"
#include "Variable.hpp"

#include <cstring>

Cvars *cvars;

Cvars::Cvars()
	: locked(true) {
	this->hasLoaded = true;
}
int Cvars::Dump(std::ofstream &file, int filter, bool values) {
	this->Lock();

	auto InternalDump = [&](ConCommandBase *cmd, std::string games, bool isCommand, bool isSAR) {		
		auto json = json11::Json::object{
			{"isCommand", isCommand},
			{"name", cmd->m_pszName},
			{"helpStr", cmd->m_pszHelpString},
			{"flags", cmd->m_nFlags},
			{"flagsStr", this->GetFlags(*cmd)}
		};
		if (games != "") {
			json["games"] = games;
		}
		if (!isCommand) {
			auto cvar = reinterpret_cast<ConVar *>(cmd);
			if (values && std::strcmp(cvar->m_pszDefaultValue, cvar->m_pszString) != 0 && !(cvar->m_nFlags & FCVAR_ARCHIVE)) {
				json["value"] = cvar->m_pszString;
			}
			json["default"] = cvar->m_pszDefaultValue;
			if (cvar->m_bHasMin) {
				json["min"] = cvar->m_fMinVal;
			}
			if (cvar->m_bHasMax) {
				json["max"] = cvar->m_fMaxVal;
			}
		}
		if (filter == 0) {
			json["isSAR"] = isSAR;
		}
		std::string str;
		json11::Json(json).dump(str);
		file << str;
	};

	file << "[";
	auto cmd = tier1->m_pConCommandList;
	auto count = 0;
	do {
		auto isSAR = false;
		std::string gameStr = "";

		for (const auto &var : Variable::GetList()) {
			if (var && var->ThisPtr() == cmd && !var->isReference) {
				isSAR = true;
				gameStr = Game::VersionToString(var->version);
				break;
			}
		}
		if (!isSAR) for (const auto &com : Command::GetList()) {
			if (com && com->ThisPtr() == cmd && !com->isReference) {
				isSAR = true;
				gameStr = Game::VersionToString(com->version);
				break;
			}
		}

		if (filter == 0 || (filter == 1 && !isSAR) || (filter == 2 && isSAR)) {
			if (!!strcmp(cmd->m_pszHelpString, "SAR alias command.\n") &&
				!!strcmp(cmd->m_pszHelpString, "SAR function command.\n")) {
				if (count > 0) file << ",\n";
				InternalDump(cmd, gameStr, cmd->IsCommand(), isSAR);
				++count;
			}
		}
	} while (cmd = cmd->m_pNext);
	file << "]\n";

	this->Unlock();

	return count;
}
int Cvars::DumpDoc(std::ofstream &file) {
	file << "# SAR: Cvars\n\n";
	file << "|Name|Default|Description|\n";
	file << "|---|---|---|\n";

	auto InternalDump = [&file](ConCommandBase *cmd, std::string games, bool isCommand) {
		file << "|";
		if (games != "") {
			file << "<i title=\"";
			for (unsigned i = 0; i < games.size(); ++i){
				auto c = games[i];
				if (c == '\n') {
					if (i != games.size() - 1) {
						file << "&#10;";
					}
				} else {
					file << c;
				}
			}
			file << "\">";
		}
		file << cmd->m_pszName;
		if (games != "") {
			file << "</i>";
		}
		file << "|";

		if (!isCommand) {
			auto cvar = reinterpret_cast<ConVar *>(cmd);
			file << cvar->m_pszDefaultValue;
		} else {
			file << "cmd";
		}
		file << "|";

		std::string desc = cmd->m_pszHelpString;
		if (desc[desc.size() - 1] != '\n') {
			console->Print("Cvar description does not end with a newline: %s\n", cmd->m_pszName);
		}
		desc.erase(0, desc.find_first_not_of(" \t\n"));
		desc.erase(desc.find_last_not_of(" \t\n") + 1);
		std::string escaped = "";
		for (auto c : desc) {
			if (c == '<') {
				escaped += "\\<";
			} else if (c == '|') {
				escaped += "\\|";
			} else if (c == '\\') {
				escaped += "\\\\";
			} else if (c == '\n') {
				escaped += "<br>";
			} else {
				escaped += c;
			}
		}
		file << escaped;
		file << "|\n";
	};

	struct cvar_t {
		ConCommandBase *cmd;
		std::string games;
		bool isCommand;
	};

	std::vector<cvar_t> cvarList;
	auto count = 0;
	for (const auto &var : Variable::GetList()) {
		if (var && !var->isReference) {
			cvarList.push_back({var->ThisPtr(), Game::VersionToString(var->version), false});
		}
	}
	for (const auto &com : Command::GetList()) {
		if (com && !com->isReference) {
			cvarList.push_back({com->ThisPtr(), Game::VersionToString(com->version), true});
		}
	}
	std::sort(cvarList.begin(), cvarList.end(), [](cvar_t a, cvar_t b) {
		auto compareCvar = [](std::string a, std::string b) {
			bool aPlus  = a[0] == '+', bPlus  = b[0] == '+';
			bool aMinus = a[0] == '-', bMinus = b[0] == '-';
			bool aPrefix = aPlus || aMinus, bPrefix = bPlus || bMinus;
			if (aPrefix) a = a.substr(1);
			if (bPrefix) b = b.substr(1);
			for (auto &c : a) c = std::tolower(c);
			for (auto &c : b) c = std::tolower(c);
			if (a == b) {
				if (aPlus && bMinus) return -1;
				if (aMinus && bPlus) return 1;
				if (aPrefix != bPrefix) return aPrefix ? -1 : 1;
			}
			return a.compare(b);
		};
		return compareCvar(a.cmd->m_pszName, b.cmd->m_pszName) < 0;
	});
	for (auto cvar : cvarList) {
		if (!!strcmp(cvar.cmd->m_pszHelpString, "SAR alias command.\n") &&
			!!strcmp(cvar.cmd->m_pszHelpString, "SAR function command.\n") &&
			cvar.cmd->m_pszName[0] != '_') {
			InternalDump(cvar.cmd, cvar.games, cvar.isCommand);
			++count;
		}
	}

	return count;
}
void Cvars::ListAll() {
	console->Msg("Commands:\n");
	for (auto &command : Command::GetList()) {
		if (!!command && command->isRegistered) {
			auto ptr = command->ThisPtr();
			console->Print("\n%s\n", ptr->m_pszName);
			console->Msg("%s", ptr->m_pszHelpString);
		}
	}
	console->Msg("\nVariables:\n");
	for (auto &variable : Variable::GetList()) {
		if (!variable) {
			continue;
		}

		auto ptr = variable->ThisPtr();
		if (variable->isRegistered) {
			console->Print("\n%s ", ptr->m_pszName);
			if (ptr->m_bHasMin) {
				console->Print("<number>\n");
			} else {
				console->Print("<string>\n");
			}
			console->Msg("%s", ptr->m_pszHelpString);
		} else if (variable->isReference) {
			std::string str = "";

			if (variable->hasCustomCallback && variable->isUnlocked)
				str = "(custom callback && unlocked)";
			else if (variable->hasCustomCallback)
				str += "(custom callback)";
			else if (variable->isUnlocked)
				str += "(unlocked)";

			console->Print("\n%s %s\n", ptr->m_pszName, str.c_str());
			if (std::strlen(ptr->m_pszHelpString) != 0) {
				console->Msg("%s\n", ptr->m_pszHelpString);
			}
		}
	}
}
void Cvars::PrintHelp(const CCommand &args) {
	if (args.ArgC() != 2) {
		return console->Print("help <cvar> - prints information about a cvar.\n");
	}

	auto cmd = reinterpret_cast<ConCommandBase *>(tier1->FindCommandBase(tier1->g_pCVar->ThisPtr(), args[1]));
	if (cmd) {
		auto IsCommand = reinterpret_cast<bool (*)(void *)>(Memory::VMT(cmd, Offsets::IsCommand));
		auto flags = GetFlags(*cmd);
		if (!IsCommand(cmd)) {
			auto cvar = reinterpret_cast<ConVar *>(cmd);
			console->Print("%s\n", cvar->m_pszName);
			// if it's not default, print value
			if (std::strcmp(cvar->m_pszDefaultValue, cvar->m_pszString) != 0) {
				console->Msg("Value: %s\n", cvar->m_pszString);
			}
			console->Msg("Default: %s\n", cvar->m_pszDefaultValue);
			if (cvar->m_bHasMin) {
				console->Msg("Min: %f\n", cvar->m_fMinVal);
			}
			if (cvar->m_bHasMax) {
				console->Msg("Max: %f\n", cvar->m_fMaxVal);
			}
			console->Msg("Flags: %i - %s\n", cvar->m_nFlags, flags.c_str());
			console->Msg("Description: %s\n", cvar->m_pszHelpString);
		} else {
			console->Print("%s\n", cmd->m_pszName);
			console->Msg("Flags: %i - %s\n", cmd->m_nFlags, flags.c_str());
			console->Msg("Description: %s\n", cmd->m_pszHelpString);
		}
	} else {
		console->Print("Unknown cvar name!\n");
	}
}
std::string Cvars::GetFlags(const ConCommandBase &cmd) {
	std::vector<std::string> flags {
		"none",
		"unregistered",
		"developmentonly",
		"game",
		"client",
		"hidden",
		"protected",
		"sponly",
		"archive",
		"notify",
		"userinfo",
		"printableonly",
		"unlogged",
		"never_as_string",
		"replicated",
		"cheat",
		"ss",
		"demo",
		"dontrecord",
		"ss_added",
		"release",
		"reload_materials",
		"reload_textures",
		"not_connected",
		"material_system_thread",
		"archive_gameconsole",
		"accessible_from_threads",
		"f26", "f27",
		"server_can_execute",
		"server_cannot_query",
		"clientcmd_can_execute",
		"f31"
	};
	auto result = std::string();
	for (auto i = -1; i < 32; ++i) {
		if ((i == -1 && cmd.m_nFlags == 0) || cmd.m_nFlags & (1 << i)) {
			if (result != "") result += " ";
			result += flags[i + 1];
		}
	}
	return result;
}
void Cvars::Lock() {
	if (!this->locked) {
		sv_accelerate.Lock();
		sv_airaccelerate.Lock();
		sv_friction.Lock();
		sv_maxspeed.Lock();
		sv_stopspeed.Lock();
		sv_maxvelocity.Lock();
		sv_footsteps.Lock();
		net_showmsg.Lock();

		sv_bonus_challenge.Lock();
		sv_laser_cube_autoaim.Lock();
		ui_loadingscreen_transition_time.Lock();
		ui_loadingscreen_fadein_time.Lock();
		ui_loadingscreen_mintransition_time.Lock();
		ui_transition_effect.Lock();
		ui_transition_time.Lock();
		ui_pvplobby_show_offline.Lock();
		mm_session_sys_delay_create_host.Lock();
		hide_gun_when_holding.Lock();
		cl_viewmodelfov.Lock();
		r_flashlightbrightness.Lock();
		r_PortalTestEnts.Lock();

		cl_forwardspeed.Lock();
		cl_sidespeed.Lock();
		cl_backspeed.Lock();
		hidehud.Lock();

		soundfade.Lock();
		leaderboard_open.Lock();
		gameui_activate.Lock();
		gameui_allowescape.Lock();
		gameui_preventescape.Lock();
		setpause.Lock();
		snd_ducktovolume.Lock();
		say.Lock();

		this->locked = true;
	}
}
void Cvars::Unlock() {
	if (this->locked) {
		sv_accelerate.Unlock();
		sv_airaccelerate.Unlock();
		sv_friction.Unlock();
		sv_maxspeed.Unlock();
		sv_stopspeed.Unlock();
		sv_maxvelocity.Unlock();
		sv_footsteps.Unlock();
		net_showmsg.Unlock();

		// Don't find a way to abuse this, ok?
		sv_bonus_challenge.Unlock(false);
		sv_laser_cube_autoaim.Unlock();
		ui_loadingscreen_transition_time.Unlock(false);
		ui_loadingscreen_fadein_time.Unlock(false);
		ui_loadingscreen_mintransition_time.Unlock(false);
		ui_transition_effect.Unlock(false);
		ui_transition_time.Unlock(false);
		ui_pvplobby_show_offline.Unlock(false);
		ui_pvplobby_show_offline.SetValue(1);
		mm_session_sys_delay_create_host.Unlock(false);
		mm_session_sys_delay_create_host.SetValue(0);
		hide_gun_when_holding.Unlock(false);
		cl_viewmodelfov.Unlock(false);
		r_flashlightbrightness.Unlock(false);
		r_flashlightbrightness.RemoveFlag(FCVAR_CHEAT);
		r_PortalTestEnts.Unlock(false);
		r_PortalTestEnts.RemoveFlag(FCVAR_CHEAT);

		cl_forwardspeed.Unlock(false);
		cl_sidespeed.Unlock(false);
		cl_backspeed.Unlock(false);
		cl_forwardspeed.RemoveFlag(FCVAR_CHEAT);
		cl_sidespeed.RemoveFlag(FCVAR_CHEAT);
		cl_backspeed.RemoveFlag(FCVAR_CHEAT);
		hidehud.Unlock(false);
		hidehud.RemoveFlag(FCVAR_CHEAT);

		soundfade.Unlock(false);
		leaderboard_open.Unlock(false);
		gameui_activate.Unlock(false);
		gameui_allowescape.Unlock(false);
		gameui_preventescape.Unlock(false);
		setpause.Unlock(false);
		snd_ducktovolume.Unlock(false);
		say.Unlock(false);
		soundfade.AddFlag(FCVAR_CLIENTCMD_CAN_EXECUTE);
		leaderboard_open.AddFlag(FCVAR_CLIENTCMD_CAN_EXECUTE | FCVAR_SERVER_CAN_EXECUTE);
		gameui_activate.AddFlag(FCVAR_CLIENTCMD_CAN_EXECUTE | FCVAR_SERVER_CAN_EXECUTE);
		gameui_allowescape.AddFlag(FCVAR_CLIENTCMD_CAN_EXECUTE | FCVAR_SERVER_CAN_EXECUTE);
		gameui_preventescape.AddFlag(FCVAR_CLIENTCMD_CAN_EXECUTE | FCVAR_SERVER_CAN_EXECUTE);
		setpause.AddFlag(FCVAR_SERVER_CAN_EXECUTE);
		snd_ducktovolume.AddFlag(FCVAR_SERVER_CAN_EXECUTE);
		say.AddFlag(FCVAR_CLIENTCMD_CAN_EXECUTE | FCVAR_SERVER_CAN_EXECUTE);

		this->locked = false;
	}
}
