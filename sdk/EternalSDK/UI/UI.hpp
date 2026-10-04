#pragma once
#include <cstdint>
#include <string>
namespace eternal::sdk::ui {
/* DTOs for future native UI adapters; Phase 1.5 publishes no UI service. */
inline constexpr bool serviceImplemented=false;
enum class ThemeToken:uint32_t { Body,Heading,Button,Back,Notice,Hud };
enum class NativeColour:uint32_t { Black,DarkBlue,DarkGreen,DarkAqua,DarkRed,DarkPurple,Gold,Grey,DarkGrey,Blue,Green,Aqua,Red,LightPurple,Yellow,White };
struct ButtonText {std::string text;ThemeToken token{ThemeToken::Button};};
} // namespace eternal::sdk::ui
