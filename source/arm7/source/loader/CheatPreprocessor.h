#pragma once
#include "../../include/picoLoader7.h"

/// @brief Class for preprocessing cheats to get rid of hacks that try to modify the cheat engine.
class CheatPreprocessor
{
public:
    /// @brief Preprocesses the given \p cheat.
    /// @param cheat The cheat to preprocess.
    void PreprocessCheat(pload_cheat_t* cheat) const;

private:
    void PatchD4Hack(pload_cheat_t* cheat, u64 hackPattern, u8 patchedInstructionFlag) const;
    void PatchDBFix(pload_cheat_t* cheat) const;
    void PatchAsmHack(pload_cheat_t* cheat) const;
};
