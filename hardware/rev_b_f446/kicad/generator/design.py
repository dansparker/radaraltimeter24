# Netzliste Rev. B (STM32F446 WeAct, K-LC1a, TLV9064) -- gleicher Inhalt wie ../../schaltplan_gen.py
# comps: ref, lib, sym, value, footprint, pins {pin: net|None}, block, grp (Platzierungsgruppe)
NC = None
POWER_NETS = {'GND'}
comps = []


def add(ref, lib, sym, value, fp, pins, block, grp=None, **kw):
    comps.append(dict(ref=ref, lib=lib, sym=sym, value=value, fp=fp, pins=pins, block=block, grp=grp or block, **kw))


FP = {'R': 'Resistor_SMD:R_0603_1608Metric', 'C': 'Capacitor_SMD:C_0603_1608Metric',
      'C0805': 'Capacitor_SMD:C_0805_2012Metric', 'C1206': 'Capacitor_SMD:C_1206_3216Metric',
      'C1210': 'Capacitor_SMD:C_1210_3225Metric', 'C1812': 'Capacitor_SMD:C_1812_4532Metric',
      'TA': 'Capacitor_Tantalum_SMD:CP_EIA-3528-21_Kemet-B', 'FB': 'Inductor_SMD:L_0603_1608Metric',
      'L': 'Inductor_SMD:L_Bourns-SRN4018', 'SMA': 'Diode_SMD:D_SMA', 'SMB': 'Diode_SMD:D_SMB',
      'TSSOP14': 'Package_SO:TSSOP-14_4.4x5mm_P0.65mm', 'SOT23': 'Package_TO_SOT_SMD:SOT-23'}


def R(ref, val, a, b, blk, grp=None):
    add(ref, 'Device', 'R', val, FP['R'], {'1': a, '2': b}, blk, grp)


def C(ref, val, a, b, blk, fp='C', grp=None, **kw):
    add(ref, 'Device', 'C', val, FP[fp], {'1': a, '2': b}, blk, grp, **kw)


def CP(ref, val, plus, minus, blk, grp=None):
    add(ref, 'Device', 'C_Polarized', val, FP['TA'], {'1': plus, '2': minus}, blk, grp)


# ------------------------------------------------------------ 1 Versorgung
B = 'PWR'
add('J1', 'Connector_Generic', 'Conn_01x06', 'JST-GH 6-pol.', 'Connector_JST:JST_GH_SM06B-GHS-TB_1x06-1MP_P1.25mm_Horizontal',
    {'1': 'VIN_RAW', '2': 'GND', '3': 'CANH', '4': 'CANL', '5': 'TX_EXT', '6': 'RX_EXT'}, B, 'J1')
add('F1', 'Device', 'Fuse', '0,5 A T', 'Fuse:Fuse_1206_3216Metric', {'1': 'VIN_RAW', '2': 'VIN_FUSE'}, B)
add('D1', 'Diode', 'SS210', 'SS210', FP['SMA'], {'1': 'VIN_PROT', '2': 'VIN_FUSE'}, B)
add('D2', 'Device', 'D_TVS', 'SMBJ33CA', FP['SMB'], {'1': 'VIN_PROT', '2': 'GND'}, B)
C('C1', '4,7µ/100V X7R', 'VIN_PROT', 'GND', B, 'C1210')
add('L1', 'Device', 'L', '10µH', FP['L'], {'1': 'VIN_PROT', '2': 'VIN_F'}, B)
C('C2', '4,7µ/100V X7R', 'VIN_F', 'GND', B, 'C1210')
add('U3', 'Regulator_Switching', 'LMR16006YQ', 'LMR16006XDDCR', 'Package_TO_SOT_SMD:SOT-23-6',
    {'5': 'VIN_F', '4': 'VIN_F', '2': 'GND', '1': 'BUCK_CB', '6': 'BUCK_SW', '3': 'BUCK_FB'}, B)
C('C3', '100n', 'BUCK_CB', 'BUCK_SW', B)
add('D4', 'Diode', 'B160-E3', 'B160', FP['SMA'], {'1': 'BUCK_SW', '2': 'GND'}, B)
add('L2', 'Device', 'L', '22µH', FP['L'], {'1': 'BUCK_SW', '2': 'V_PRE'}, B)
C('C4', '22µ/16V X7R', 'V_PRE', 'GND', B, 'C1206')
R('R1', '68k 1%', 'V_PRE', 'BUCK_FB', B)
R('R2', '10k 1%', 'BUCK_FB', 'GND', B)
add('U4', 'Regulator_Linear', 'TLV76750DRVx', 'TLV76750DRVR', 'Package_SON:WSON-6-1EP_2x2mm_P0.65mm_EP1x1.6mm',
    {'6': 'V_PRE', '4': 'V_PRE', '3': 'GND', '5': 'GND', '7': 'GND', '1': '5V_D', '2': '5V_D'}, B)
C('C5', '10µ X7R', '5V_D', 'GND', B, 'C0805')
add('FB1', 'Device', 'FerriteBead_Small', '600R@100MHz', FP['FB'], {'1': 'V_PRE', '2': 'VPRE_F'}, B, 'RFPWR')
C('C6', '10µ X7R', 'VPRE_F', 'GND', B, 'C0805', 'RFPWR')
add('U5', 'Regulator_Linear', 'ADP7142ARDZ-5.0', 'ADP7142ARDZ-5.0', 'Package_SO:SOIC-8-1EP_3.9x4.9mm_P1.27mm_EP2.29x3mm',
    {'7': 'VPRE_F', '8': 'VPRE_F', '5': 'RF_EN', '4': 'GND', '9': 'GND', '1': '5V_RF', '2': '5V_RF', '3': '5V_RF',
     '6': 'LDO_SS'}, B, 'RFPWR')
C('C16', '10n', 'LDO_SS', 'GND', B, grp='RFPWR')
C('C7', '10µ X7R', '5V_RF', 'GND', B, 'C0805', 'RFPWR')
add('FB2', 'Device', 'FerriteBead_Small', '600R@100MHz', FP['FB'], {'1': '5V_D', '2': 'A3V3_IN'}, B, 'IF')
C('C8', '1µ X7R', 'A3V3_IN', 'GND', B, grp='IF')
add('U6', 'Regulator_Linear', 'LP5907MFX-3.3', 'LP5907MFX-3.3', 'Package_TO_SOT_SMD:SOT-23-5',
    {'1': 'A3V3_IN', '3': 'A3V3_IN', '2': 'GND', '4': NC, '5': 'A3V3'}, B, 'IF')
C('C9', '1µ X7R', 'A3V3', 'GND', B, grp='IF')

# ------------------------------------------------------------ 2 Mikrocontroller (WeAct-Board auf Buchsenleisten)
B = 'MCU'
# Kopfleisten siehe mklib.py (WEACT_PINS); belegt werden nur die Pins hier, der Rest bleibt offen
J2 = {'2': 'GND', '13': 'DAC_RAMP', '17': 'ADC_IF', '20': 'MON_VTUNE', '19': 'MON_5VRF', '22': 'MON_VIN',
      '11': 'UART_TX', '14': 'UART_RX', '29': '3V3', '30': 'GND',
      '31': 'CAN_TX', '32': 'GND', '34': 'CAN_RX', '51': 'GAIN_A', '52': 'GAIN_B', '55': 'RF_EN', '56': 'DBG',
      '57': '5V_MCU', '58': '5V_MCU', '59': 'GND', '60': 'GND'}
add('J2', 'radar_revb', 'WeAct_STM32F4_64Pin', 'WeAct STM32F446 Core Board', 'radar_revb:WeAct_STM32F4_64Pin_CoreBoard',
    {str(i): J2.get(str(i)) for i in range(1, 61)}, B, 'J2')
add('D3', 'Diode', 'SS14', 'SS14', FP['SMA'], {'1': '5V_MCU', '2': '5V_D'}, B, 'AUX')
R('R3', '100k', 'RF_EN', 'GND', B, 'AUX')
add('TP1', 'Connector', 'TestPoint', 'DBG PB13', 'TestPoint:TestPoint_Pad_D1.5mm', {'1': 'DBG'}, B, 'AUX')

# ------------------------------------------------------------ 3 VCO-Ansteuerung
B = 'VCO'
R('R10', '10k 1%', 'DAC_RAMP', 'VCO_DIV', B)
R('R11', '15k 1%', 'VCO_DIV', 'GND', B)
C('C10', '2,2n C0G', 'VCO_DIV', 'VCO_DRV', B)          # Sallen-Key: Rueckkopplung vom ersten Knoten
R('R12', '6,2k 1%', 'VCO_DIV', 'VCO_SK', B)
C('C11', '1n C0G', 'VCO_SK', 'GND', B)
R('R13', '47', 'VCO_DRV', 'VCO_TUNE', B)
C('C12', 'DNP', 'VCO_TUNE', 'GND', B, dnp=True)
R('R14', '10k', 'VCO_TUNE', 'MON_VTUNE', B)
C('C13', '1n', 'MON_VTUNE', 'GND', B)
add('U2', 'Amplifier_Operational', 'TLV9064', 'TLV9064IPWR', FP['TSSOP14'],
    {'3': 'VCO_SK', '2': 'VCO_DRV', '1': 'VCO_DRV',          # A: VCO-Treiber
     '5': 'AA2_N2', '6': 'AA2_OUT', '7': 'AA2_OUT',          # B: Anti-Alias Stufe 2
     '10': 'VMID', '9': 'U2C_OUT', '8': 'U2C_OUT',           # C, D unbenutzt
     '12': 'VMID', '13': 'U2D_OUT', '14': 'U2D_OUT',
     '4': 'A3V3', '11': 'GND'}, B)
C('C37', '100n', 'A3V3', 'GND', B)

# ------------------------------------------------------------ 4 Radarmodul (Unterseite, Antenne nach unten)
B = 'RF'
add('M1', 'radar_revb', 'K-LC1a', 'K-LC1a', 'radar_revb:RFbeam_K-LC1a_Bottom',
    {'1': NC, '2': '5V_RF', '3': 'IF_RAW', '4': 'GND', '5': 'VCO_TUNE'}, B, 'M1')
CP('C14', '10µ Ta', '5V_RF', 'GND', B, 'IF')
C('C15', '100n', '5V_RF', 'GND', B, grp='IF')

# ------------------------------------------------------------ 5 Schnittstellen und Ueberwachung
B = 'IO'
add('U8', 'Interface_CAN_LIN', 'TJA1042T-3', 'TCAN1042VDRQ1', 'Package_SO:SOIC-8_3.9x4.9mm_P1.27mm',
    {'1': 'CAN_TX', '4': 'CAN_RX', '8': 'GND', '3': '5V_D', '5': '3V3', '2': 'GND', '7': 'CANH', '6': 'CANL'}, B)
C('C39', '100n', '5V_D', 'GND', B)
C('C41', '100n', '3V3', 'GND', B)
add('D5', 'Power_Protection', 'NUP2105L', 'NUP2105L', FP['SOT23'], {'1': 'CANH', '2': 'CANL', '3': 'GND'}, B)
add('JP1', 'Jumper', 'SolderJumper_2_Open', 'Abschluss', 'Jumper:SolderJumper-2_P1.3mm_Open_RoundedPad1.0x1.5mm',
    {'1': 'CANH', '2': 'CAN_TERM'}, B)
R('R40', '60,4', 'CAN_TERM', 'CAN_SPLIT', B)
R('R41', '60,4', 'CAN_SPLIT', 'CANL', B)
C('C40', '4,7n', 'CAN_SPLIT', 'GND', B)
R('R42', '100', 'UART_TX', 'TX_EXT', B, 'AUX')
R('R43', '100', 'RX_EXT', 'UART_RX', B, 'AUX')
add('D6', 'radar_revb', 'PESD3V3S2UT', 'PESD3V3S2UT', FP['SOT23'], {'1': 'TX_EXT', '2': 'RX_EXT', '3': 'GND'}, B, 'AUX')
R('R44', '150k 1%', 'VIN_F', 'MON_VIN', B, 'AUX')
R('R45', '10k 1%', 'MON_VIN', 'GND', B, 'AUX')
C('C34', '100n', 'MON_VIN', 'GND', B, grp='AUX')
R('R46', '10k 1%', '5V_RF', 'MON_5VRF', B, 'AUX')
R('R47', '10k 1%', 'MON_5VRF', 'GND', B, 'AUX')
C('C35', '100n', 'MON_5VRF', 'GND', B, grp='AUX')

# ------------------------------------------------------------ 6 ZF-Kette
B = 'IF'
R('R20', '1k', 'IF_RAW', 'GND', B)
C('C20', '100p C0G', 'IF_RAW', 'GND', B)
C('C21', '1µ Folie PPS/PET', 'IF_RAW', 'IF_AC', B, 'C1812')
R('R21', '4,7k', 'IF_AC', 'VMID1', B)
R('R22', '4,7k 1%', 'IF_FB1', 'IF1', B)
R('R23', '470 1%', 'IF_FB1', 'IF_G1', B)
CP('C22', '22µ Ta', 'IF_G1', 'GND', B)
C('C23', '1,5n C0G', 'IF1', 'IF_HP', B)
R('R24', '1k', 'IF_HP', 'VMID', B)
R('R25', '100k', 'IF_FB2', 'IF2', B)
R('R29', '390 1%', 'IF_FB2', 'IF_G2', B)
CP('C24', '10µ Ta', 'IF_G2', 'GND', B)
add('U7', 'radar_revb', 'TS3A5017PW', 'TS3A5017PWR', 'Package_SO:TSSOP-16_4.4x5mm_P0.65mm',
    {'1': 'GND', '2': 'GAIN_B', '3': 'IF2', '4': 'GSW3', '5': 'GSW2', '6': 'GSW1', '7': 'IF_FB2', '8': 'GND',
     '9': NC, '10': NC, '11': NC, '12': NC, '13': NC, '14': 'GAIN_A', '15': 'A3V3', '16': 'A3V3'}, B)
C('C38', '100n', 'A3V3', 'GND', B)
R('R26', '27k 1%', 'GSW1', 'IF2', B)
R('R27', '5,6k 1%', 'GSW2', 'IF2', B)
R('R28', '1,2k 1%', 'GSW3', 'IF2', B)
R('R30', '1,5k 1%', 'IF2', 'AA1_N1', B)
C('C25', '1n C0G', 'AA1_N1', 'AA1_OUT', B)
R('R31', '1,5k 1%', 'AA1_N1', 'AA1_N2', B)
C('C26', '820p C0G', 'AA1_N2', 'GND', B)
R('R37', '1,5k 1%', 'AA1_OUT', 'AA2_N1', B)
C('C32', '2,2n C0G', 'AA2_N1', 'AA2_OUT', B)
R('R38', '1,5k 1%', 'AA2_N1', 'AA2_N2', B)
C('C33', '330p C0G', 'AA2_N2', 'GND', B)
R('R32', '470', 'AA2_OUT', 'ADC_IF', B)
C('C27', '2,2n C0G', 'ADC_IF', 'GND', B)
add('U1', 'Amplifier_Operational', 'TLV9064', 'TLV9064IPWR', FP['TSSOP14'],
    {'3': 'IF_AC', '2': 'IF_FB1', '1': 'IF1',                 # A: Stufe 1, V = 11
     '5': 'IF_HP', '6': 'IF_FB2', '7': 'IF2',                 # B: Stufe 2, umschaltbar
     '10': 'AA1_N2', '9': 'AA1_OUT', '8': 'AA1_OUT',          # C: Anti-Alias Stufe 1
     '12': 'VMID_DIV', '13': 'VMID_BUF', '14': 'VMID_BUF',    # D: VMID-Puffer
     '4': 'A3V3', '11': 'GND'}, B)
C('C36', '100n', 'A3V3', 'GND', B)
R('R33', '10k', 'A3V3', 'VMID_DIV', B)
R('R34', '10k', 'VMID_DIV', 'GND', B)
CP('C28', '10µ Ta', 'VMID_DIV', 'GND', B)
R('R35', '47', 'VMID_BUF', 'VMID', B)
CP('C29', '10µ Ta', 'VMID', 'GND', B)
C('C30', '100n', 'VMID', 'GND', B)
R('R36', '1k', 'VMID', 'VMID1', B)
CP('C31', '10µ Ta', 'VMID1', 'GND', B)

# ------------------------------------------------------------ Mechanik, PWR_FLAGs
for i in range(1, 5):
    add('H%d' % i, 'Mechanical', 'MountingHole_Pad', 'M3', 'MountingHole:MountingHole_3.2mm_M3_Pad_Via', {'1': 'GND'}, 'MECH', 'H')
for i, n in enumerate(['GND', 'VIN_RAW', 'VIN_PROT', 'VIN_F', 'VPRE_F', 'A3V3_IN', '3V3', 'V_PRE']):
    add('#FLG%02d' % (i + 1), 'power', 'PWR_FLAG', 'PWR_FLAG', '', {'1': n}, 'MECH', virtual=True)
