// VGN / VXE / Redragon-OEM peripherals: vid, pid, protocol family, channel, usage page, usage,
// report id and whether the id is the 2.4 GHz dongle. Exported verbatim from VGN VHUB 4.0.11's
// deviceList (see docs/vgn.md).
//
// Six ids that VGN also uses were left out because drivers that run earlier already claim them:
//   3151:5038 (rongyuan.cpp), 320F:5055 and 320F:5088 (atk_keyboard.cpp),
//   3554:F505 and 3554:F506 (kysona.cpp), 3554:FA09 (aula.cpp / kysona.cpp).
    { 0x258A, 0x010C, Family::Beiying,   Channel::Feature, 0xFF00, 1  , 6  , false },  // VXE V87pro
    { 0x258A, 0x024E, Family::Beiying,   Channel::Feature, 0xFF00, 1  , 9  , false },  // VGN V98pro V3
    { 0x258A, 0x024F, Family::Beiying,   Channel::Output, 0xFF02, 2  , 19 , true  },  // VGN V98pro V3
    { 0x258A, 0x026A, Family::Beiying,   Channel::Feature, 0xFF00, 1  , 9  , false },  // VGN V87 V2
    { 0x258A, 0x027B, Family::Beiying,   Channel::Output, 0xFF02, 2  , 19 , true  },  // VGN V87 V2
    { 0x258A, 0x027E, Family::Beiying,   Channel::Feature, 0xFF00, 1  , 9  , false },  // VGN V98pro V3
    { 0x258A, 0x027F, Family::Beiying,   Channel::Output, 0xFF02, 2  , 19 , true  },  // VGN V98pro V3
    { 0x3151, 0x502D, Family::Rongyuan,  Channel::Feature, 0xFFFF, 2  , 0  , false },  // VGN Neon75 He
    { 0x3151, 0x502F, Family::Rongyuan,  Channel::Feature, 0xFFFF, 2  , 0  , false },  // VGN Neon75
    { 0x3151, 0x5054, Family::Rongyuan,  Channel::Feature, 0xFFFF, 2  , 0  , false },  // VGN Neon75 Extreme Tri-mode Keyboard
    { 0x3151, 0x5055, Family::Rongyuan,  Channel::Feature, 0xFFFF, 2  , 0  , true  },  // VGN Neon75 Extreme 2.4G Dongle
    { 0x3554, 0xF502, Family::Da,        Channel::Output, 0xFF02, 2  , 8  , false },  // 
    { 0x3554, 0xF503, Family::Da,        Channel::Output, 0xFF02, 2  , 8  , true  },  // 
    { 0x3554, 0xF50A, Family::Da,        Channel::Output, 0xFF02, 2  , 8  , false },  // 
    { 0x3554, 0xF50B, Family::Da,        Channel::Output, 0xFF02, 2  , 8  , true  },  // 
    { 0x3554, 0xF511, Family::Da,        Channel::Output, 0xFF02, 2  , 8  , false },  // VGN F1 SE
    { 0x3554, 0xFB3D, Family::Da,        Channel::Output, 0xFF02, 2  , 8  , false },  // VGN Y2 Ultra
    { 0x3554, 0xFB3E, Family::Da,        Channel::Output, 0xFF02, 2  , 8  , true  },  // VGN Y2 Ultra
    { 0x3554, 0xFB49, Family::Da,        Channel::Output, 0xFF02, 2  , 8  , true  },  // VGN USB Receiver
    { 0x3554, 0xFB4A, Family::Da,        Channel::Output, 0xFF02, 2  , 8  , true  },  // UFO Pro
    { 0x3554, 0xFB4B, Family::Da,        Channel::Output, 0xFF02, 2  , 8  , true  },  // UFO Ultra
    { 0x3554, 0xFB50, Family::Da,        Channel::Output, 0xFF02, 2  , 8  , true  },  // VGN F1 SE
    { 0x3554, 0xFB51, Family::Da,        Channel::Output, 0xFF02, 2  , 8  , false },  // VGN F1 SE
    { 0x3554, 0xFB52, Family::Da,        Channel::Output, 0xFF02, 2  , 8  , true  },  // VGN Y2 Master
    { 0x3554, 0xFB53, Family::Da,        Channel::Output, 0xFF02, 2  , 8  , false },  // VGN Y2 Master
    { 0x3554, 0xFB54, Family::Da,        Channel::Output, 0xFF02, 2  , 8  , true  },  // VGN Y2 Pro Max
    { 0x3554, 0xFB55, Family::Da,        Channel::Output, 0xFF02, 2  , 8  , false },  // VGN Y2 Pro Max
    { 0x3554, 0xFB56, Family::Da,        Channel::Output, 0xFF02, 2  , 8  , true  },  // VGN F2 Master
    { 0x3554, 0xFB57, Family::Da,        Channel::Output, 0xFF02, 2  , 8  , false },  // VGN F2 Master
    { 0x3554, 0xFB58, Family::Da,        Channel::Output, 0xFF02, 2  , 8  , true  },  // VGN USB Receiver
    { 0x3554, 0xFB59, Family::Da,        Channel::Output, 0xFF02, 2  , 8  , false },  // Dragonfly 3 Master
    { 0x372E, 0x10AC, Family::King,      Channel::Output, 0xFF10, 16 , 32 , false },  // VGN Dragonfly King
    { 0x372E, 0x10AD, Family::King,      Channel::Output, 0xFF10, 16 , 32 , true  },  // VGN Dragonfly King
    { 0x373B, 0x1028, Family::Weisheng,  Channel::Output, 0xFF1C, 146, 4  , false },  // VGN S87
    { 0x373B, 0x1029, Family::Weisheng,  Channel::Output, 0xFF1C, 146, 4  , true  },  // VGN S87 Dongle
    { 0x3837, 0x4042, Family::Yongjiaxin, Channel::Output, 0xFF01, 16 , 0  , false },  // VGN Dragonfly3 SE
    { 0x391D, 0x1001, Family::Yongjiaxin, Channel::Output, 0xFF01, 16 , 0  , false },  // VGN Dragonfly3 SE
    { 0x391D, 0x1003, Family::Yongjiaxin, Channel::Output, 0xFF01, 16 , 0  , false },  // VGN Dragonfly3 Pro
    { 0x391D, 0x1005, Family::Da,        Channel::Output, 0xFF02, 2  , 8  , false },  // Dragonfly 3 Master
    { 0x391D, 0x100A, Family::Yongjiaxin, Channel::Output, 0xFF01, 16 , 0  , false },  // Dragonfly Y2 V2 Pro
    { 0x391D, 0x100B, Family::Yongjiaxin, Channel::Output, 0xFF01, 16 , 0  , false },  // Dragonfly Y2 V2 Plus
    { 0x391D, 0x1017, Family::Yongjiaxin, Channel::Output, 0xFF01, 16 , 0  , false },  // Dragonfly F1 V2 Pro
    { 0x391D, 0x1019, Family::Yongjiaxin, Channel::Output, 0xFF01, 16 , 0  , false },  // Dragonfly F1 V2 SE
    { 0x391D, 0x101B, Family::LdMs,      Channel::Output, 0xFFA0, 2  , 160, false },  // Dragonfly F1 V2 Extreme
    { 0x391D, 0x101D, Family::Da,        Channel::Output, 0xFF02, 2  , 8  , false },  // Dragonfly 3 MasterGT
    { 0x391D, 0x1023, Family::Kc2,       Channel::Output, 0xFFC1, 1  , 0  , false },  // Dragonfly 3 Turbo
    { 0x391D, 0x1024, Family::Kc2,       Channel::Output, 0xFFC1, 1  , 0  , false },  // Dragonfly 3 Turbo+
    { 0x391D, 0x1025, Family::Kc2,       Channel::Output, 0xFFC1, 1  , 0  , false },  // Dragonfly F1 V5 Turbo
    { 0x391D, 0x1026, Family::Kc2,       Channel::Output, 0xFFC1, 1  , 0  , false },  // Dragonfly F1 V5 Turbo+
    { 0x391D, 0x102D, Family::Da,        Channel::Output, 0xFF02, 2  , 8  , true  },  // Dragonfly F2 MasterGT
    { 0x391D, 0x102E, Family::LdMs,      Channel::Output, 0xFFA0, 2  , 160, false },  // Falcon3 Extreme
    { 0x391D, 0x1032, Family::Da,        Channel::Output, 0xFF02, 2  , 8  , false },  // Dragonfly F2 MasterGT
    { 0x391D, 0x1034, Family::Da,        Channel::Output, 0xFF02, 2  , 8  , false },  // Dragonfly Y2 V2 MasterGT
    { 0x391D, 0x1035, Family::Da,        Channel::Output, 0xFF02, 2  , 8  , false },  // Dragonfly F2 Turbo
    { 0x391D, 0x1036, Family::Da,        Channel::Output, 0xFF02, 2  , 8  , false },  // Dragonfly F2 Turbo+
    { 0x391D, 0x1037, Family::LdMs,      Channel::Output, 0xFFA0, 2  , 160, false },  // Falcon3 Extreme+
    { 0x391D, 0x1038, Family::Da,        Channel::Output, 0xFF02, 2  , 8  , false },  // Dragonfly F1 V2 SE+
    { 0x391D, 0x1039, Family::Da,        Channel::Output, 0xFF02, 2  , 8  , true  },  // Dragonfly 3 MINI SE+
    { 0x391D, 0x103C, Family::Kc2,       Channel::Output, 0xFFC1, 1  , 0  , false },  // Dragonfly 3 MINI Turbo
    { 0x391D, 0x103D, Family::Kc2,       Channel::Output, 0xFFC1, 1  , 0  , false },  // Dragonfly 3 MINI Turbo+
    { 0x391D, 0x1042, Family::Kc2,       Channel::Output, 0xFFC1, 1  , 0  , false },  // Dragonfly F1 V5 Air Turbo
    { 0x391D, 0x1043, Family::Kc2,       Channel::Output, 0xFFC1, 1  , 0  , false },  // Dragonfly F1 V5 Air Turbo+
    { 0x391D, 0x1045, Family::Kc2,       Channel::Output, 0xFFC1, 1  , 0  , false },  // Dragonfly 3 RT+
    { 0x391D, 0x1A01, Family::Yongjiaxin, Channel::Output, 0xFF01, 16 , 0  , true  },  // VGN Dragonfly3 SE
    { 0x391D, 0x1A03, Family::Yongjiaxin, Channel::Output, 0xFF01, 16 , 0  , true  },  // VGN Dragonfly3 Pro
    { 0x391D, 0x1A05, Family::Da,        Channel::Output, 0xFF02, 2  , 8  , true  },  // VGN USB Receiver
    { 0x391D, 0x1A09, Family::Yongjiaxin, Channel::Output, 0xFF01, 16 , 0  , true  },  // 2.4G Wireless Mouse
    { 0x391D, 0x1A0A, Family::Yongjiaxin, Channel::Output, 0xFF01, 16 , 0  , true  },  // 2.4G Wireless Mouse
    { 0x391D, 0x1A14, Family::Yongjiaxin, Channel::Output, 0xFF01, 16 , 0  , true  },  // 2.4G Wireless Mouse
    { 0x391D, 0x1A16, Family::Yongjiaxin, Channel::Output, 0xFF01, 16 , 0  , true  },  // 2.4G Wireless Mouse
    { 0x391D, 0x1A18, Family::LdMs,      Channel::Output, 0xFFA0, 2  , 160, true  },  // Dragonfly F1 V2 Extreme 2.4G
    { 0x391D, 0x1A1A, Family::Da,        Channel::Output, 0xFF02, 2  , 8  , true  },  // Dragonfly 3 MasterGT
    { 0x391D, 0x1A20, Family::Kc2,       Channel::Output, 0xFFC1, 1  , 0  , true  },  // Dragonfly 3 Turbo
    { 0x391D, 0x1A21, Family::Kc2,       Channel::Output, 0xFFC1, 1  , 0  , true  },  // Dragonfly 3 Turbo+
    { 0x391D, 0x1A22, Family::Kc2,       Channel::Output, 0xFFC1, 1  , 0  , true  },  // Dragonfly F1 V5 Turbo 2.4G
    { 0x391D, 0x1A23, Family::Kc2,       Channel::Output, 0xFFC1, 1  , 0  , true  },  // Dragonfly F1 V5 Turbo+ 2.4G
    { 0x391D, 0x1A29, Family::LdMs,      Channel::Output, 0xFFA0, 2  , 160, true  },  // Falcon3 Extreme+ 2.4G
    { 0x391D, 0x1A2A, Family::LdMs,      Channel::Output, 0xFFA0, 2  , 160, true  },  // Falcon3 Extreme 2.4G
    { 0x391D, 0x1A2F, Family::Da,        Channel::Output, 0xFF02, 2  , 8  , true  },  // Dragonfly Y2 V2 MasterGT
    { 0x391D, 0x1A30, Family::Da,        Channel::Output, 0xFF02, 2  , 8  , true  },  // Dragonfly F2 Turbo
    { 0x391D, 0x1A31, Family::Da,        Channel::Output, 0xFF02, 2  , 8  , true  },  // Dragonfly F2 Turbo+
    { 0x391D, 0x1A32, Family::Da,        Channel::Output, 0xFF02, 2  , 8  , true  },  // Dragonfly F1 V2 SE+
    { 0x391D, 0x1A33, Family::Da,        Channel::Output, 0xFF02, 2  , 8  , false },  // Dragonfly 3 MINI SE+
    { 0x391D, 0x1A35, Family::Kc2,       Channel::Output, 0xFFC1, 1  , 0  , true  },  // Dragonfly 3 MINI Turbo
    { 0x391D, 0x1A36, Family::Kc2,       Channel::Output, 0xFFC1, 1  , 0  , true  },  // Dragonfly 3 MINI Turbo+
    { 0x391D, 0x1A3B, Family::Kc2,       Channel::Output, 0xFFC1, 1  , 0  , true  },  // Dragonfly F1 V5 Air Turbo 2.4G
    { 0x391D, 0x1A3C, Family::Kc2,       Channel::Output, 0xFFC1, 1  , 0  , true  },  // Dragonfly F1 V5 Air Turbo+ 2.4G
    { 0x391D, 0x1A3E, Family::Kc2,       Channel::Output, 0xFFC1, 1  , 0  , true  },  // Dragonfly 3 RT+
    { 0x391D, 0x2001, Family::Rongyuan,  Channel::Feature, 0xFFFF, 2  , 0  , false },  // VGN Neon68 Extreme
    { 0x391D, 0x2003, Family::Beiying,   Channel::Feature, 0xFF00, 1  , 9  , false },  // VGN N75 V2
    { 0x391D, 0x2004, Family::Beiying,   Channel::Feature, 0xFF00, 1  , 9  , false },  // VGN N75 V2
    { 0x391D, 0x2005, Family::Beiying,   Channel::Feature, 0xFF00, 1  , 9  , false },  // VGN V108
    { 0x391D, 0x2006, Family::Beiying,   Channel::Feature, 0xFF00, 1  , 9  , false },  // VGN V108
    { 0x391D, 0x200D, Family::Rongyuan,  Channel::Feature, 0xFFFF, 2  , 0  , false },  // VGN Neon75 Air
    { 0x391D, 0x200F, Family::Rongyuan,  Channel::Feature, 0xFFFF, 2  , 0  , false },  // VGN Neon75 Air
    { 0x391D, 0x2016, Family::Rongyuan,  Channel::Feature, 0xFFFF, 2  , 0  , false },  // VGN Neon75 Turbo
    { 0x391D, 0x2A01, Family::Beiying,   Channel::Output, 0xFF02, 2  , 19 , true  },  // VGN N75 V2
    { 0x391D, 0x2A02, Family::Beiying,   Channel::Output, 0xFF02, 2  , 19 , true  },  // VGN V108
    { 0x391D, 0x2A05, Family::Rongyuan,  Channel::Feature, 0xFFFF, 2  , 0  , true  },  // VGN Neon75 Air 2.4G
    { 0x391D, 0x2A07, Family::Rongyuan,  Channel::Feature, 0xFFFF, 2  , 0  , true  },  // VGN Neon75 Turbo2.4G
    { 0x9981, 0x6002, Family::King,      Channel::Output, 0xFF10, 16 , 32 , false },  // VGN Dragonfly King
    { 0x9981, 0x7002, Family::King,      Channel::Output, 0xFF10, 16 , 32 , true  },  // VGN Dragonfly King
    { 0xA8A4, 0x2355, Family::Tjx,       Channel::Output, 0xFF01, 16 , 240, false },  // VGN N87
    { 0xA8A5, 0x2255, Family::Tjx,       Channel::Output, 0xFF01, 16 , 240, true  },  // VGN N87
