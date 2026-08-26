local seen = 0
local function checkpoint(address, value)
  local s = emu.getState()
  print(string.format("RESET_PHASE pc=%04X cpuCycle=%s scanline=%s ppuCycle=%s ppuMaster=%s frame=%s",
    address, tostring(s["cpu.cycleCount"]), tostring(s["ppu.scanline"]), tostring(s["ppu.cycle"]), tostring(s["ppu.masterClock"]), tostring(s["frameCount"])))
  seen = seen + 1
  if seen >= 2 then emu.stop(0) end
end
emu.addMemoryCallback(checkpoint, emu.callbackType.exec, 0xFFAE, 0xFFAE)
emu.addMemoryCallback(checkpoint, emu.callbackType.exec, 0xE010, 0xE010)
