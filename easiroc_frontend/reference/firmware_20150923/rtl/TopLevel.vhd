--------------------------------------------------------------------------------
--! @file   TopLevel.vhd
--! @brief  Toplevel entity of VME-EASIROC
--! @author Naruhiro Chikuma
--! @date   2015-09-06
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;
use work.RegisterAddress.all;
use work.Asynch.all;
use work.SynchronizedDate.all;

entity TopLevel is
    port(
        EXTCLK50M : in  std_logic; -- External Clock(50MHz)

        -- AT93C46D
        EEPROM_CS : out std_logic;
        EEPROM_SK : out std_logic;
        EEPROM_DI : out std_logic;
        EEPROM_DO : in  std_logic;

        -- PHY(100Mbps only)
        ETH_RSTn   : out std_logic;
        ETH_TX_CLK : in  std_logic;
        ETH_TX_EN  : out std_logic;
        ETH_TXD    : out std_logic_vector(3 downto 0);
        ETH_TX_ER  : out std_logic;
        ETH_RX_CLK : in  std_logic;
        ETH_RX_DV  : in  std_logic;
        ETH_RXD    : in  std_logic_vector(3 downto 0);
        ETH_RX_ER  : in  std_logic;
        ETH_CRS    : in  std_logic;
        ETH_COL    : in  std_logic;
        ETH_MDC    : out std_logic;
        ETH_MDIO   : inout std_logic;
	ETH_LED    : out std_logic_vector(2 downto 1);
        DIP_SW     : in std_logic_vector(0 downto 0);

        -- EASIROC1
        -- Direct Control
        EASIROC1_HOLDB    : out std_logic;
        EASIROC1_RESET_PA : out std_logic;
        EASIROC1_PWR_ON   : out std_logic;
        EASIROC1_VAL_EVT  : out std_logic;
        EASIROC1_RAZ_CHN  : out std_logic;
        -- Slow Control
        EASIROC1_CLK_SR    : out std_logic;
        EASIROC1_RSTB_SR   : out std_logic;
        EASIROC1_SRIN_SR   : out std_logic;
        EASIROC1_LOAD_SC   : out std_logic;
        EASIROC1_SELECT_SC : out std_logic;
        -- read register
        EASIROC1_CLK_READ   : out std_logic;
        EASIROC1_RSTB_READ  : out std_logic;
        EASIROC1_SRIN_READ  : out std_logic;
        -- ADC
        EASIROC1_ADC_CLK_HG  : out std_logic;
        EASIROC1_ADC_DATA_HG : in  std_logic_vector(11 downto 0);
        EASIROC1_ADC_OTR_HG  : in  std_logic;
        EASIROC1_ADC_CLK_LG  : out std_logic;
        EASIROC1_ADC_DATA_LG : in  std_logic_vector(11 downto 0);
        EASIROC1_ADC_OTR_LG  : in  std_logic;
        -- TDC
        EASIROC1_TRIGGER : in std_logic_vector(31 downto 0);

        -- EASIROC2
        -- Direct Control
        EASIROC2_HOLDB    : out std_logic;
        EASIROC2_RESET_PA : out std_logic;
        EASIROC2_PWR_ON   : out std_logic;
        EASIROC2_VAL_EVT  : out std_logic;
        EASIROC2_RAZ_CHN  : out std_logic;
        -- Slow Control
        EASIROC2_CLK_SR    : out std_logic;
        EASIROC2_RSTB_SR   : out std_logic;
        EASIROC2_SRIN_SR   : out std_logic;
        EASIROC2_LOAD_SC   : out std_logic;
        EASIROC2_SELECT_SC : out std_logic;
        -- read register
        EASIROC2_CLK_READ  : out std_logic;
        EASIROC2_RSTB_READ : out std_logic;
        EASIROC2_SRIN_READ : out std_logic;
        -- ADC
        EASIROC2_ADC_CLK_HG  : out std_logic;
        EASIROC2_ADC_DATA_HG : in  std_logic_vector(11 downto 0);
        EASIROC2_ADC_OTR_HG  : in  std_logic;
        EASIROC2_ADC_CLK_LG  : out std_logic;
        EASIROC2_ADC_DATA_LG : in  std_logic_vector(11 downto 0);
        EASIROC2_ADC_OTR_LG  : in  std_logic;
        -- TDC
        EASIROC2_TRIGGER : in std_logic_vector(31 downto 0);

        -- SPI FLASH
        SPI_SCLK  : out std_logic;
        SPI_SS_N  : out std_logic;
        SPI_MOSI  : out std_logic;
        SPI_MISO  : in  std_logic;
    	PROG_B_ON : out std_logic;
	
    	-- LED Control
    	LED : out std_logic_vector(8 downto 1);

    	-- Test charge injection
    	CAL1    : out std_logic;
    	CAL2    : out std_logic;
    	PWR_RST : in  std_logic;

    	-- Monitor ADC
    	MUX       : out std_logic_vector(3 downto 0);
    	MUX_EN    : out std_logic_vector(3 downto 0);
    	CS_MADC   : out std_logic;
    	DIN_MADC  : out std_logic;
    	SCK_MADC  : out std_logic;
    	DOUT_MADC : in  std_logic;

    	-- HV Control
    	SCK_DAC : out std_logic;
    	SDI_DAC : out std_logic;
    	CS_DAC  : out std_logic;
    	HV_EN   : out std_logic;

    	-- User I/O
    	IN_FPGA         : in  std_logic_vector(6 downto 1);
    	OUT_FPGA        : out std_logic_vector(5 downto 1);
    	OR32_C1         : in  std_logic;
    	OR32_C2         : in  std_logic;
    	DIGITAL_LINE_C1 : in  std_logic;
    	DIGITAL_LINE_C2 : in  std_logic

    );
end TopLevel;

architecture RTL of TopLevel is

    constant C_VERSION : std_logic_vector(15 downto 0) := X"4_0_1_0";

    component ClockManager is
        port(
            EXT_CLK              : in  std_logic; -- 50MHz
            RESET                : in  std_logic;
            LOCKED               : out std_logic; -- PLL Locked
            SITCP_CLK            : out  std_logic;-- 25MHz
            SLOWCONTROL_CLK      : out  std_logic;-- 6MHz
            ADC_CLK              : out  std_logic;-- 6MHz
            AD9220_CLK           : out  std_logic;-- 3MHz (Synchronized with ADC_CLK)
            AD9220_CLK_OUT       : out std_logic; -- 3MHz for AD9220
            AD9220_CLK_ENABLE    : in std_logic;  -- AD9220 CLK Enable
            TDC_CLK              : out std_logic; -- 125MHz
            TDC_SAMPLING_CLK_0   : out std_logic; -- 250MHz 0degree
            TDC_SAMPLING_CLK_90  : out std_logic; -- 250MHz 90degree
            TDC_SAMPLING_CLK_180 : out std_logic; -- 250MHz 180degree
            TDC_SAMPLING_CLK_270 : out std_logic; -- 250MHz 270degree
            FAST_CLK             : out std_logic; -- 500MHz
            SPI_CLK              : out std_logic  -- 66MHz
        );
    end component;

    component ResetManager
        port(
            CLK          : in std_logic;
            PLL_LOCKED   : in std_logic;
            TCP_OPEN_ACK : in std_logic;
            L1_RESET     : out std_logic;
            L2_RESET     : out std_logic;
            L3_RESET     : out std_logic;
            L4_RESET     : out std_logic
        );
    end component;

    component WRAP_SiTCP_GMII_XC7A_32K
        port(
            CLK            : in std_logic;
            RST            : in std_logic;
            FORCE_DEFAULTn : in std_logic;
            EXT_IP_ADDR    : in std_logic_vector(31 downto 0);
            EXT_TCP_PORT   : in std_logic_vector(15 downto 0);
            EXT_RBCP_PORT  : in std_logic_vector(15 downto 0);
            PHY_ADDR       : in std_logic_vector(4 downto 0);
            EEPROM_DO      : in std_logic;
            GMII_1000M     : in std_logic;
            GMII_TX_CLK    : in std_logic;
            GMII_RX_CLK    : in std_logic;
            GMII_RX_DV     : in std_logic;
            GMII_RXD       : in std_logic_vector(7 downto 0);
            GMII_RX_ER     : in std_logic;
            GMII_CRS       : in std_logic;
            GMII_COL       : in std_logic;
            GMII_MDIO_IN   : in std_logic;
            TCP_OPEN_REQ   : in std_logic;
            TCP_CLOSE_ACK  : in std_logic;
            TCP_RX_WC      : in std_logic_vector(15 downto 0);
            TCP_TX_WR      : in std_logic;
            TCP_TX_DATA    : in std_logic_vector(7 downto 0);
            RBCP_ACK       : in std_logic;
            RBCP_RD        : in std_logic_vector(7 downto 0);
            EEPROM_CS      : out std_logic;
            EEPROM_SK      : out std_logic;
            EEPROM_DI      : out std_logic;
            USR_REG_X3C    : out std_logic_vector(7 downto 0);
            USR_REG_X3D    : out std_logic_vector(7 downto 0);
            USR_REG_X3E    : out std_logic_vector(7 downto 0);
            USR_REG_X3F    : out std_logic_vector(7 downto 0);
            GMII_RSTn      : out std_logic;
            GMII_TX_EN     : out std_logic;
            GMII_TXD       : out std_logic_vector(7 downto 0);
            GMII_TX_ER     : out std_logic;
            GMII_MDC       : out std_logic;
            GMII_MDIO_OUT  : out std_logic;
            GMII_MDIO_OE   : out std_logic;
            SiTCP_RST      : out std_logic;
            TCP_OPEN_ACK   : out std_logic;
            TCP_ERROR      : out std_logic;
            TCP_CLOSE_REQ  : out std_logic;
            TCP_RX_WR      : out std_logic;
            TCP_RX_DATA    : out std_logic_vector(7 downto 0);
            TCP_TX_FULL    : out std_logic;
            RBCP_ACT       : out std_logic;
            RBCP_ADDR      : out std_logic_vector(31 downto 0);
            RBCP_WD        : out std_logic_vector(7 downto 0);
            RBCP_WE        : out std_logic;
            RBCP_RE        : out std_logic
        );
    end component;

    component RBCP_Distributor
        port(
            ACK_IN1 : in std_logic;
            ACK_IN2 : in std_logic;
            ACK_IN3 : in std_logic;
            ACK_IN4 : in std_logic;
            ACK_IN5 : in std_logic;
            ACK_IN6 : in std_logic;
            ACK_IN7 : in std_logic;
            ACK_IN8 : in std_logic;
            ACK_IN9 : in std_logic;
            ACK_INA : in std_logic;
            ACK_INB : in std_logic;
            ACK_INC : in std_logic;
            ACK_IND : in std_logic;
            RD_IN1  : in std_logic_vector(7 downto 0);
            RD_IN2  : in std_logic_vector(7 downto 0);
            RD_INA  : in std_logic_vector(7 downto 0);
            RD_OUT  : out std_logic_vector(7 downto 0);
            ACK_OUT : out std_logic
        );
    end component;

    component Version
        generic (
            G_VERSION_ADDR     : std_logic_vector(31 downto 0);
            G_VERSION          : std_logic_vector(15 downto 0);
            G_SYNTHESIZED_DATE : std_logic_vector(31 downto 0)
        );
        port(
            CLK       : in std_logic;
            RESET     : in std_logic;
            RBCP_ACT  : in std_logic;
            RBCP_ADDR : in std_logic_vector(31 downto 0);
            RBCP_RE   : in std_logic;
            RBCP_RD   : out std_logic_vector(7 downto 0);
            RBCP_ACK  : out std_logic
        );
    end component;

    component DirectControl
        generic(
            G_DIRECT_CONTROL_ADDR : std_logic_vector(31 downto 0)
        );
        port(
            CLK             : in std_logic;
            RESET           : in std_logic;
            RBCP_ACT        : in std_logic;
            RBCP_ADDR       : in std_logic_vector(31 downto 0);
            RBCP_WE         : in std_logic;
            RBCP_WD         : in std_logic_vector(7 downto 0);
            RBCP_ACK        : out std_logic;
            RAZ_CHN1        : out std_logic;
            VAL_EVT1        : out std_logic;
            RESET_PA1       : out std_logic;
            PWR_ON1         : out std_logic;
            SELECT_SC1      : out std_logic;
            LOAD_SC1        : out std_logic;
            RSTB_SR1        : out std_logic;
            RSTB_READ1      : out std_logic;
            RAZ_CHN2        : out std_logic;
            VAL_EVT2        : out std_logic;
            RESET_PA2       : out std_logic;
            PWR_ON2         : out std_logic;
            SELECT_SC2      : out std_logic;
            LOAD_SC2        : out std_logic;
            RSTB_SR2        : out std_logic;
            RSTB_READ2      : out std_logic;
            START_SC_CYCLE1 : out std_logic;
            START_SC_CYCLE2 : out std_logic
        );
    end component;

    component GlobalSlowControl
        generic(
            G_SLOW_CONTROL1_ADDR : std_logic_vector(31 downto 0);
            G_SLOW_CONTROL2_ADDR : std_logic_vector(31 downto 0)
        );
        port(
            CLK              : in std_logic;
            RESET            : in std_logic;
            SLOW_CONTROL_CLK : in std_logic;
            RBCP_ACT         : in std_logic;
            RBCP_ADDR        : in std_logic_vector(31 downto 0);
            RBCP_WE          : in std_logic;
            RBCP_WD          : in std_logic_vector(7 downto 0);
            START_CYCLE1     : in std_logic;
            SELECT_SC1       : in std_logic;
            START_CYCLE2     : in std_logic;
            SELECT_SC2       : in std_logic;
            RBCP_ACK         : out std_logic;
            SRIN_SR1         : out std_logic;
            CLK_SR1          : out std_logic;
            SRIN_SR2         : out std_logic;
            CLK_SR2          : out std_logic
        );
    end component;

    component GlobalReadRegister
        generic(
            G_READ_REGISTER1_ADDR : std_logic_vector(31 downto 0);
            G_READ_REGISTER2_ADDR : std_logic_vector(31 downto 0)
        );
        port(
            CLK               : in std_logic;
            RESET             : in std_logic;
            READ_REGISTER_CLK : in std_logic;
            RBCP_ACT          : in std_logic;
            RBCP_ADDR         : in std_logic_vector(31 downto 0);
            RBCP_WE           : in std_logic;
            RBCP_WD           : in std_logic_vector(7 downto 0);
            RBCP_ACK          : out std_logic;
            SRIN_READ1        : out std_logic;
            CLK_READ1         : out std_logic;
            SRIN_READ2        : out std_logic;
            CLK_READ2         : out std_logic
        );
    end component;

    component TriggerManager is
        port(
            SITCP_CLK  : in  std_logic;
            ADC_CLK    : in std_logic;
            AD9220_CLK : in std_logic;
            TDC_CLK    : in std_logic;
            SCALER_CLK : in std_logic;
            FAST_CLK   : in std_logic;
            RESET      : in  std_logic;

            -- Trigger
            HOLD       : in std_logic;
            L1_TRIGGER : in std_logic;
            L2_TRIGGER : in std_logic;
            FAST_CLEAR : in std_logic;
            BUSY       : out std_logic;

            -- Sender interface
            TRANSMIT_START : out std_logic;
            GATHERER_BUSY  : in std_logic;

            -- Control
            IS_DAQ_MODE  : in std_logic;
            TCP_OPEN_ACK : in std_logic;

            -- ADC interface
            ADC_TRIGGER    : out std_logic;
            ADC_FAST_CLEAR : out std_logic;
            ADC_BUSY       : in std_logic;

            -- TDC intreface
            COMMON_STOP    : out std_logic;
            TDC_FAST_CLEAR : out std_logic;
            TDC_BUSY       : in std_logic;

            -- Scaler interface
            SCALER_TRIGGER    : out std_logic;
            SCALER_FAST_CLEAR : out std_logic;
            SCALER_BUSY       : in std_logic;

            -- Hold
            HOLD_OUT1_N : out std_logic;
            HOLD_OUT2_N : out std_logic
        );
    end component;

    component TriggerWidth is
	generic(
	  G_TRIGGER_WIDTH_ADDRESS : std_logic_vector(31 downto 0)
        );
	port(
          CLK          : in  std_logic;
          RESET        : in  std_logic;
          RBCP_ACT     : in  std_logic;
          RBCP_ADDR    : in  std_logic_vector(31 downto 0);
          RBCP_WE      : in  std_logic;
          RBCP_WD      : in  std_logic_vector(7 downto 0);
          RBCP_ACK     : out std_logic;
    	  INTERVAL_CLK : in  std_logic;
          TRIGGER_IN1  : in  std_logic_vector(31 downto 0);
          TRIGGER_IN2  : in  std_logic_vector(31 downto 0);
          TRIGGER_OUT1 : out std_logic_vector(31 downto 0);
          TRIGGER_OUT2 : out std_logic_vector(31 downto 0)
	);
    end component;

    component StatusRegister
        generic(
            G_STATUS_REGISTER_ADDR : std_logic_vector(31 downto 0)
        );
        port(
            CLK         : in std_logic;
            RESET       : in std_logic;
            RBCP_ACT    : in std_logic;
            RBCP_ADDR   : in std_logic_vector(31 downto 0);
            RBCP_WE     : in std_logic;
            RBCP_WD     : in std_logic_vector(7 downto 0);
            RBCP_ACK    : out std_logic;
            DAQ_MODE    : out std_logic;
            SEND_ADC    : out std_logic;
            SEND_TDC    : out std_logic;
            SEND_SCALER : out std_logic
        );
    end component;

    component ADC
        generic(
            G_PEDESTAL_SUPPRESSION_ADDR : std_logic_vector(31 downto 0)
        );
        port(
            SITCP_CLK         : in std_logic;
            ADC_CLK           : in std_logic;
            AD9220_CLK        : in std_logic;
            RESET             : in std_logic;
            TRIGGER           : in std_logic;
            FAST_CLEAR        : in std_logic;
            ADC_DATA_HG1      : in std_logic_vector(11 downto 0);
            ADC_OTR_HG1       : in std_logic;
            ADC_DATA_LG1      : in std_logic_vector(11 downto 0);
            ADC_OTR_LG1       : in std_logic;
            ADC_DATA_HG2      : in std_logic_vector(11 downto 0);
            ADC_OTR_HG2       : in std_logic;
            ADC_DATA_LG2      : in std_logic_vector(11 downto 0);
            ADC_OTR_LG2       : in std_logic;
            ADC_RADDR_HG1     : in std_logic_vector(5 downto 0);
            ADC_RCOMP_HG1     : in std_logic;
            ADC_RADDR_LG1     : in std_logic_vector(5 downto 0);
            ADC_RCOMP_LG1     : in std_logic;
            ADC_RADDR_HG2     : in std_logic_vector(5 downto 0);
            ADC_RCOMP_HG2     : in std_logic;
            ADC_RADDR_LG2     : in std_logic_vector(5 downto 0);
            ADC_RCOMP_LG2     : in std_logic;
            RBCP_ACT          : in std_logic;
            RBCP_ADDR         : in std_logic_vector(31 downto 0);
            RBCP_WE           : in std_logic;
            RBCP_WD           : in std_logic_vector(7 downto 0);
            BUSY              : out std_logic;
            AD9220_CLK_ENABLE : out std_logic;
            CLK_READ1         : out std_logic;
            RSTB_READ1        : out std_logic;
            SRIN_READ1        : out std_logic;
            CLK_READ2         : out std_logic;
            RSTB_READ2        : out std_logic;
            SRIN_READ2        : out std_logic;
            ADC_DOUT_HG1      : out std_logic_vector(20 downto 0);
            ADC_EMPTY_HG1     : out std_logic;
            ADC_DOUT_LG1      : out std_logic_vector(20 downto 0);
            ADC_EMPTY_LG1     : out std_logic;
            ADC_DOUT_HG2      : out std_logic_vector(20 downto 0);
            ADC_EMPTY_HG2     : out std_logic;
            ADC_DOUT_LG2      : out std_logic_vector(20 downto 0);
            ADC_EMPTY_LG2     : out std_logic;
            RBCP_ACK          : out std_logic
        );
    end component;

    component MHTDC is
        generic(
            G_TIME_WINDOW_REGISTER_ADDRESS : std_logic_vector(31 downto 0) := X"00000000"
        );
        port(
            TDC_CLK   : in std_logic;  -- 125MHz
            CLK_0     : in std_logic;  -- 250MHz 0degree
            CLK_90    : in std_logic;  -- 250MHz 90degree
            CLK_180   : in std_logic;  -- 250MHz 180degree
            CLK_270   : in std_logic;  -- 250MHz 270degree
            SITCP_CLK : in std_logic;
            RESET     : in std_logic;

            DIN         : in std_logic_vector(63 downto 0);
            COMMON_STOP : in std_logic;
            FAST_CLEAR  : in std_logic;

            -- RBCP Interface
            RBCP_ACT  : in std_logic;
            RBCP_ADDR : in std_logic_vector(31 downto 0);
            RBCP_WE   : in std_logic;
            RBCP_WD   : in std_logic_vector(7 downto 0);
            RBCP_ACK  : out std_logic;

            BUSY : out std_logic;

            DOUT_L  : out std_logic_vector(19 downto 0);
            RADDR_L : in std_logic_vector(10 downto 0);
            RCOMP_L : in std_logic;
            EMPTY_L : out std_logic;

            DOUT_T  : out std_logic_vector(19 downto 0);
            RADDR_T : in std_logic_vector(10 downto 0);
            RCOMP_T : in std_logic;
            EMPTY_T : out std_logic
        );
    end component;

    component ScalerTimer is
        port(
            SCALER_CLK : in std_logic; -- 125MHz
            RESET      : in std_logic;
            TIMER_1MHZ : out std_logic;
            TIMER_1KHZ : out std_logic
        );
    end component;

    component Scaler is
        port (
            SCALER_CLK  : in std_logic;
            SITCP_CLK   : in std_logic;
            RESET       : in std_logic;
            RESET_TIMER : out std_logic;

            -- Data input
            DIN : in std_logic_vector(68 downto 0); -- EASIROC TRIGGER 64CH
                                                    -- OR32U, OR32D, OR64, 1kHz, 1MHz

            -- Control Interface
            L1_TRIGGER : in std_logic; -- Synchronized with SCALER_CLK
            FAST_CLEAR : in std_logic;
            BUSY       : out std_logic;

            -- Gatherer interface
            DOUT  : out std_logic_vector(20 downto 0);
            RADDR : in std_logic_vector(6 downto 0);
            RCOMP : in std_logic;
            EMPTY : out std_logic
        );
    end component;

    component GlobalGatherer is
        port(
            CLK   : in  std_logic;
            RESET : in  std_logic;

            -- ADC0
            ADC0_DIN   : in  std_logic_vector (20 downto 0);
            ADC0_RADDR : out  std_logic_vector (5 downto 0);
            ADC0_RCOMP : out  std_logic;
            ADC0_EMPTY : in  std_logic;

            -- ADC1
            ADC1_DIN   : in  std_logic_vector (20 downto 0);
            ADC1_RADDR : out  std_logic_vector (5 downto 0);
            ADC1_RCOMP : out  std_logic;
            ADC1_EMPTY : in  std_logic;

            -- ADC2
            ADC2_DIN   : in  std_logic_vector (20 downto 0);
            ADC2_RADDR : out  std_logic_vector (5 downto 0);
            ADC2_RCOMP : out  std_logic;
            ADC2_EMPTY : in  std_logic;

            -- ADC3
            ADC3_DIN   : in  std_logic_vector (20 downto 0);
            ADC3_RADDR : out  std_logic_vector (5 downto 0);
            ADC3_RCOMP : out  std_logic;
            ADC3_EMPTY : in  std_logic;

            -- TDC (Leading)
            TDC_DIN_L   : in  std_logic_vector (19 downto 0);
            TDC_RADDR_L : out  std_logic_vector (10 downto 0);
            TDC_RCOMP_L : out  std_logic;
            TDC_EMPTY_L : in  std_logic;

            -- TDC (Trailing)
            TDC_DIN_T   : in  std_logic_vector (19 downto 0);
            TDC_RADDR_T : out  std_logic_vector (10 downto 0);
            TDC_RCOMP_T : out  std_logic;
            TDC_EMPTY_T : in  std_logic;

            -- Scaler
            SCALER_DIN   : in std_logic_vector(20 downto 0);
            SCALER_RADDR : out std_logic_vector(6 downto 0);
            SCALER_RCOMP : out std_logic;
            SCALER_EMPTY : in std_logic;

            -- FIFO
            DOUT : out std_logic_vector(31 downto 0);
            WE   : out std_logic;
            FULL : in std_logic;

            -- Control
            SEND_ADC    : in  std_logic;
            SEND_TDC    : in  std_logic;
            SEND_SCALER : in std_logic;
            TRIGGER     : in  std_logic;
            BUSY        : out  std_logic
        );
    end component;

    component GlobalSender
        port(
            CLK          : in std_logic;
            RESET        : in std_logic;
            DIN          : in std_logic_vector(31 downto 0);
            WE           : in std_logic;
            TCP_TX_FULL  : in std_logic;
            TCP_OPEN_ACK : in std_logic;
            FULL         : out std_logic;
            TCP_TX_DATA  : out std_logic_vector(7 downto 0);
            TCP_TX_WR    : out std_logic
        );
    end component;

    component SPI_FLASH_Programmer is
        generic(
            G_SPI_FLASH_PROGRAMMER_ADDRESS : std_logic_vector(31 downto 0);
            G_SITCP_CLK_FREQ : real;
            G_SPI_CLK_FREQ : real
        );
        port(
            SPI_CLK   : in std_logic;
            SITCP_CLK : in std_logic;
            RESET     : in std_logic;

            RBCP_ACT  : in std_logic;
            RBCP_ADDR : in std_logic_vector(31 downto 0);
            RBCP_WE   : in std_logic;
            RBCP_WD   : in std_logic_vector(7 downto 0);
            RBCP_RE   : in std_logic;
            RBCP_RD   : out std_logic_vector(7 downto 0);
            RBCP_ACK  : out std_logic;

            SPI_SCLK : out std_logic;
            SPI_SS_N : out std_logic;
            SPI_MOSI : out std_logic;
            SPI_MISO : in std_logic;
            RECONFIGURATION_N : out std_logic
        );
    end component;

    component SelectableLogic is
       generic(
           G_SELECTABLE_LOGIC_ADDRESS : std_logic_vector(31 downto 0) := X"00000000"
       );
       port(
           CLK         : in std_logic;
           RESET       : in std_logic;
           TRIGGER_IN1 : in std_logic_vector(31 downto 0);
           TRIGGER_IN2 : in std_logic_vector(31 downto 0);
           RBCP_ACT    : in std_logic;
           RBCP_ADDR   : in std_logic_vector(31 downto 0);
           RBCP_WE     : in std_logic;
           RBCP_WD     : in std_logic_vector(7 downto 0);
           RBCP_ACK    : out std_logic;
           SELECTABLE_LOGIC : out std_logic
       );
   end component;

    component DiscriOr is
        port(
            EASIROC1_TRIGGER : in std_logic_vector(31 downto 0);
            EASIROC2_TRIGGER : in std_logic_vector(31 downto 0);
            OR32U : out std_logic;
            OR32D : out std_logic;
            OR64  : out std_logic
        );
    end component;

    component ReadRegisterSelector is
        port (
            DAQ_MODE      : in std_logic;
            CLK_READ_ADC  : in std_logic;
            RSTB_READ_ADC : in std_logic;
            SRIN_READ_ADC : in std_logic;
            CLK_READ      : in std_logic;
            RSTB_READ     : in std_logic;
            SRIN_READ     : in std_logic;
            EASIROC_CLK_READ  : out std_logic;
            EASIROC_RSTB_READ : out std_logic;
            EASIROC_SRIN_READ : out std_logic
        );
    end component;

    Component TestChargeInjection is
        port(
            CLK  : in  std_logic; 
            RST  : in  std_logic; 
            CAL1 : out std_logic; 
            CAL2 : out std_logic
    );
    end component;

    component HVControl is
	    generic(
	    	G_HV_CONTROL_ADDR : std_logic_vector(31 downto 0) := X"00000000"
	    );
	    port(
		CLK        : in  std_logic;
		RST        : in  std_logic;
    	-- RBCP interface
		RBCP_ACT   : in  std_logic;
	    	RBCP_ADDR  : in  std_logic_vector(31 downto 0);
	    	RBCP_WD    : in  std_logic_vector(7 downto 0);
		RBCP_WE    : in  std_logic;
		RBCP_ACK   : out std_logic;
    	-- DAC output
		SDI_DAC    : out std_logic;
		SCK_DAC    : out std_logic;
		CS_DAC     : out std_logic;
		HV_EN      : out std_logic;
		-- LED Contorl
		DOUT_LED   : out std_logic_vector(15 downto 0)
	);
    end component;

    component MADC is
	generic(
	    G_MONITOR_ADC_ADDR : std_logic_vector(31 downto 0) := X"00000000";
	    G_READ_MADC_ADDR   : std_logic_vector(31 downto 0) := X"00000000"
    	);	    
    	port(
    	    CLK        : in  std_logic;
    	    RST        : in  std_logic;
    	    
    	    -- RBCP interface
    	    RBCP_ACT   : in  std_logic;
    	    RBCP_ADDR  : in  std_logic_vector(31 downto 0);
    	    RBCP_WD    : in  std_logic_vector(7 downto 0);
    	    RBCP_WE    : in  std_logic;
    	    RBCP_ACK   : out std_logic;
    	    RBCP_RD    : out std_logic_vector(7 downto 0);
    	    RBCP_RE    : in  std_logic;

    		-- Monitor ADC
    	    DOUT_MADC  : in  std_logic;
    	    DIN_MADC   : out std_logic;
    	    CS_MADC    : out std_logic;
    	    SCK_MADC   : out std_logic;
    	    MUX_EN     : out std_logic_vector(3 downto 0);
    	    MUX        : out std_logic_vector(3 downto 0)
    	);
    end component;


    component LEDControl is
	    port(
               CLK        : in  std_logic;
               TcpOpenAck : in  std_logic;
               RBCP_ADDR  : in  std_logic_vector(31 downto 0);
    	       DIN	  : in  std_logic_vector(15 downto 0);
               BUF1       : in  std_logic;
               Busy       : in  std_logic;
               LED1       : out std_logic;
               LED2       : out std_logic;
               LED3       : out std_logic;
               LED4       : out std_logic;
               LED5       : out std_logic;
               LED6       : out std_logic;
               LED7       : out std_logic;
               LED8       : out std_logic
    );
    end component;

    component UsrClkOut is
    	generic(
    	    G_USER_OUTPUT_ADDR : std_logic_vector(31 downto 0) := X"00000000"
    	);	    	
    	port(
    	    CLK_25M    : in  std_logic;
    	    RST        : in  std_logic;
    	    	-- RBCP interface
    	    RBCP_ACT   : in  std_logic;
    	    RBCP_ADDR  : in  std_logic_vector(31 downto 0);
    	    RBCP_WD    : in  std_logic_vector(7 downto 0);
    	    RBCP_WE    : in  std_logic;
    	    RBCP_ACK   : out std_logic;
    		-- clock in
    	    CLK_500M   : in  std_logic;
    	    CLK_125M   : in  std_logic;
    	    CLK_3M     : in  std_logic;
    		-- out
	    DOUT       : out std_logic
    	);
    end component;

    -- Clock
    signal SitcpClk          : std_logic;
    signal Locked            : std_logic;

    signal SlowControlClk    : std_logic;
    signal AdcClk            : std_logic;
    signal Ad9220Clk         : std_logic;
    signal Ad9220ClkOut      : std_logic;
    signal Ad9220ClkEnable   : std_logic;
    signal TdcClk            : std_logic;
    signal TdcSamplingClk0   : std_logic;
    signal TdcSamplingClk90  : std_logic;
    signal TdcSamplingClk180 : std_logic;
    signal TdcSamplingClk270 : std_logic;
    signal FastClk           : std_logic;
    signal SpiClk            : std_logic;
    signal ScalerClk         : std_logic;

    -- Reset
    signal L1Reset : std_logic;
    signal L2Reset : std_logic;
    signal L3Reset : std_logic;
    signal L4Reset : std_logic;

    signal ClockManagerReset       : std_logic;
    signal SitcpReset              : std_logic;
    signal SlowControlReset        : std_logic;
    signal ReadRegisterReset       : std_logic;
    signal DirectControlReset      : std_logic;
    signal StatusRegisterReset     : std_logic;
    signal VersionReset            : std_logic;
    signal AdcReset                : std_logic;
    signal TdcReset                : std_logic;
    signal HoldReset               : std_logic;
    signal TriggerManagerReset     : std_logic;
    signal GathererReset           : std_logic;
    signal SenderReset             : std_logic;
    signal SpiFlashProgrammerReset : std_logic;
    signal SelectableLogicReset    : std_logic;
    signal ScalerReset             : std_logic;
    signal ScalerTimerReset        : std_logic;

    -- PHY
    signal MY_GMII_TXD : std_logic_vector(7 downto 0);
    signal MY_GMII_RXD : std_logic_vector(7 downto 0);
    signal MDIO_OE     : std_logic;
    signal MDIO_OUT    : std_logic;

    -- TCP
    signal TcpOpenAck  : std_logic;
    signal TcpCloseReq : std_logic;
    signal TcpTxData   : std_logic_vector(7 downto 0);
    signal TcpTxWr     : std_logic;
    signal TcpTxFull   : std_logic;

    -- RBCP
    signal RBCP_ACT  : std_logic;
    signal RBCP_ADDR : std_logic_vector(31 downto 0);
    signal RBCP_WE   : std_logic;
    signal RBCP_WD   : std_logic_vector(7 downto 0);
    signal RBCP_RE   : std_logic;
    signal RBCP_RD   : std_logic_vector(7 downto 0);
    signal RBCP_ACK  : std_logic;

    signal RbcpRdVersion             : std_logic_vector(7 downto 0);
    signal RbcpRdSpiFlashPromgrammer : std_logic_vector(7 downto 0);
    signal RbcpRdMonitorADC          : std_logic_vector(7 downto 0);
    signal RbcpAckVersion            : std_logic;
    signal RbcpAckDirectControl      : std_logic;
    signal RbcpAckSlowControl        : std_logic;
    signal RbcpAckReadRegister       : std_logic;
    signal RbcpAckStatusREgister     : std_logic;
    signal RbcpAckAdc                : std_logic;
    signal RbcpAckSpiFlashProgrammer : std_logic;
    signal RbcpAckSelectableLogic    : std_logic;
    signal RbcpAckMhtdc              : std_logic;
    signal RbcpAckMadc               : std_logic;
    signal RbcpAckHVControl          : std_logic;
    signal RbcpAckUsrClkOut          : std_logic;
    signal RbcpAckTriggerWidth       : std_logic;

    -- SlowControl
    signal int_EASIROC1_SELECT_SC : std_logic;
    signal int_EASIROC2_SELECT_SC : std_logic;

    signal StartScCycle1 : std_logic;
    signal StartScCycle2 : std_logic;

    signal ClkReadReadRegister1  : std_logic;
    signal RstbReadReadRegister1 : std_logic;
    signal SrinReadReadRegister1 : std_logic;
    signal ClkReadReadRegister2  : std_logic;
    signal RstbReadReadRegister2 : std_logic;
    signal SrinReadReadRegister2 : std_logic;

    -- ADC
    signal ClkReadAdc1  : std_logic;
    signal RstbReadAdc1 : std_logic;
    signal SrinReadAdc1 : std_logic;
    signal ClkReadAdc2  : std_logic;
    signal RstbReadAdc2 : std_logic;
    signal SrinReadAdc2 : std_logic;

    signal AdcTrigger   : std_logic;
    signal AdcFastClear : std_logic;
    signal AdcBusy      : std_logic;
    signal AdcDoutHg1   : std_logic_vector(20 downto 0);
    signal AdcRaddrHg1  : std_logic_vector(5 downto 0);
    signal AdcRcompHg1  : std_logic;
    signal AdcEmptyHg1  : std_logic;
    signal AdcDoutLg1   : std_logic_vector(20 downto 0);
    signal AdcRaddrLg1  : std_logic_vector(5 downto 0);
    signal AdcRcompLg1  : std_logic;
    signal AdcEmptyLg1  : std_logic;
    signal AdcDoutHg2   : std_logic_vector(20 downto 0);
    signal AdcRaddrHg2  : std_logic_vector(5 downto 0);
    signal AdcRcompHg2  : std_logic;
    signal AdcEmptyHg2  : std_logic;
    signal AdcDoutLg2   : std_logic_vector(20 downto 0);
    signal AdcRaddrLg2  : std_logic_vector(5 downto 0);
    signal AdcRcompLg2  : std_logic;
    signal AdcEmptyLg2  : std_logic;

    -- DirectControl
    signal DaqMode    : std_logic;
    signal SendAdc    : std_logic;
    signal SendTdc    : std_logic;
    signal SendScaler : std_logic;
    signal ResetExternalScalerTimer : std_logic;

    -- Control
    signal Trigger       : std_logic;
    signal Busy          : std_logic;
    signal TransmitStart : std_logic;

    -- MHTDC
    signal CommonStop       : std_logic;
    signal TdcFastClear     : std_logic;
    signal TdcDin           : std_logic_vector(63 downto 0);
    signal TdcBusy          : std_logic;
    signal TdcDoutLeading   : std_logic_vector(19 downto 0);
    signal TdcRaddrLeading  : std_logic_vector(10 downto 0);
    signal TdcRcompLeading  : std_logic;
    signal TdcEmptyLeading  : std_logic;
    signal TdcDoutTrailing  : std_logic_vector(19 downto 0);
    signal TdcRaddrTrailing : std_logic_vector(10 downto 0);
    signal TdcRcompTrailing : std_logic;
    signal TdcEmptyTrailing : std_logic;

    -- Scaler
    signal ScalerDin       : std_logic_vector(68 downto 0);
    signal ScalerDout      : std_logic_vector(20 downto 0);
    signal ScalerRaddr     : std_logic_vector(6 downto 0);
    signal ScalerRcomp     : std_logic;
    signal ScalerEmpty     : std_logic;
    signal ScalerTrigger   : std_logic;
    signal ScalerFastClear : std_logic;
    signal ScalerBusy      : std_logic;
    signal ScalerTimer1Mhz : std_logic;
    signal ScalerTimer1Khz : std_logic;
    signal Or64            : std_logic;
    signal Or32U           : std_logic;
    signal Or32D           : std_logic;

    -- Gatherer
    signal GathererBusy : std_logic;
    signal GathererDout : std_logic_vector(31 downto 0);
    signal GathererWe   : std_logic;
    signal SenderFull   : std_logic;

    -- User I/O
    signal hold_trigger    : std_logic;
    signal user_L1_trigger : std_logic;
    signal user_L2_trigger : std_logic;
    signal user_clear      : std_logic;
    signal s_IN_FPGA       : std_logic_vector(6 downto 1);
    signal user_trigger1   : std_logic;
    signal user_trigger2   : std_logic;
    signal outbuf1         : std_logic;    
    
    -- Monitor ADC & LED Control
    signal HV_LED : std_logic_vector(15 downto 0);

    -- Trigger
    signal TRIGGER1 : std_logic_vector(31 downto 0);
    signal TRIGGER2 : std_logic_vector(31 downto 0);

begin

    ClockManager_0: ClockManager
    port map(
        EXT_CLK              => EXTCLK50M,
        RESET                => ClockManagerReset,
        LOCKED               => Locked,
        SITCP_CLK            => SitcpClk,
        SLOWCONTROL_CLK      => SlowControlClk,
        ADC_CLK              => AdcClk,
        AD9220_CLK           => Ad9220Clk,
        AD9220_CLK_OUT       => Ad9220ClkOut,
        AD9220_CLK_ENABLE    => Ad9220ClkEnable,
        FAST_CLK             => FastClk,
        TDC_CLK              => TdcClk,
        TDC_SAMPLING_CLK_0   => TdcSamplingClk0,
        TDC_SAMPLING_CLK_90  => TdcSamplingClk90,
        TDC_SAMPLING_CLK_180 => TdcSamplingClk180,
        TDC_SAMPLING_CLK_270 => TdcSamplingClk270,
        SPI_CLK              => SpiClk
    );

    ScalerClk <= TdcClk;


    ResetManager_0: ResetManager
    port map(
        CLK          => SitcpClk,
        PLL_LOCKED   => Locked,
        TCP_OPEN_ACK => TcpOpenAck,
        L1_RESET     => L1Reset,
        L2_RESET     => L2Reset,
        L3_RESET     => L3Reset,
        L4_RESET     => L4Reset
    );

    ClockManagerReset       <= L4Reset;
    SitcpReset              <= L3Reset;
    SlowControlReset        <= L2Reset;
    ReadRegisterReset       <= L2Reset;
    DirectControlReset      <= L2Reset;
    StatusRegisterReset     <= L2Reset;
    VersionReset            <= L2Reset;
    AdcReset                <= L1Reset;
    TdcReset                <= L1Reset;
    HoldReset               <= L1Reset;
    TriggerManagerReset     <= L1Reset;
    GathererReset           <= L1Reset;
    SenderReset             <= L1Reset;
    SpiFlashProgrammerReset <= L1Reset;
    SelectableLogicReset    <= L2Reset;
    ScalerReset             <= L1Reset;
    ScalerTimerReset        <= ScalerReset or ResetExternalScalerTimer;


    WRAP_SiTCP_GMII_XC7A_32K_0 : WRAP_SiTCP_GMII_XC7A_32K
    port map(
        CLK            => SitcpClk,
        RST            => SitcpReset,
        FORCE_DEFAULTn => DIP_SW(0),
        EXT_IP_ADDR    => (others => '0'),
        EXT_TCP_PORT   => (others => '0'),
        EXT_RBCP_PORT  => (others => '0'),
        PHY_ADDR       => (others => '0'),
        EEPROM_CS      => EEPROM_CS,
        EEPROM_SK      => EEPROM_SK,
        EEPROM_DI      => EEPROM_DI,
        EEPROM_DO      => EEPROM_DO,
        USR_REG_X3C    => open,
        USR_REG_X3D    => open,
        USR_REG_X3E    => open,
        USR_REG_X3F    => open,
        GMII_RSTn      => ETH_RSTn,
        GMII_1000M     => '0',
        GMII_TX_CLK    => ETH_TX_CLK,
        GMII_TX_EN     => ETH_TX_EN,
        GMII_TXD       => MY_GMII_TXD,
        GMII_TX_ER     => ETH_TX_ER,
        GMII_RX_CLK    => ETH_RX_CLK,
        GMII_RX_DV     => ETH_RX_DV,
        GMII_RXD       => MY_GMII_RXD,
        GMII_RX_ER     => ETH_RX_ER,
        GMII_CRS       => ETH_CRS,
        GMII_COL       => ETH_COL,
        GMII_MDC       => ETH_MDC,
        GMII_MDIO_IN   => ETH_MDIO,
        GMII_MDIO_OUT  => MDIO_OUT,
        GMII_MDIO_OE   => MDIO_OE,
        SiTCP_RST      => open,
        TCP_OPEN_REQ   => '0', -- reserved, should be '0'
        TCP_OPEN_ACK   => TcpOpenAck,
        TCP_ERROR      => open,
        TCP_CLOSE_REQ  => TcpCloseReq,
        TCP_CLOSE_ACK  => TcpCloseReq,
        TCP_RX_WC      => (others => '0'),
        TCP_RX_WR      => open,
        TCP_RX_DATA    => open,
        TCP_TX_FULL    => TcpTxFull,
        TCP_TX_WR      => TcpTxWr,
        TCP_TX_DATA    => TcpTxData,
        RBCP_ACT       => RBCP_ACT,
        RBCP_ADDR      => RBCP_ADDR,
        RBCP_WD        => RBCP_WD,
        RBCP_WE        => RBCP_WE,
        RBCP_RE        => RBCP_RE,
        RBCP_ACK       => RBCP_ACK,
        RBCP_RD        => RBCP_RD
    );

    -- TriState buffer
    ETH_MDIO <= MDIO_OUT when(MDIO_OE = '1') else 'Z';

    ETH_TXD <= MY_GMII_TXD(3 downto 0);
    MY_GMII_RXD(3 downto 0) <= ETH_RXD;
    MY_GMII_RXD(7 downto 4) <= (others => '0');

    RBCP_Distributor_0: RBCP_Distributor
    port map(
        ACK_IN1 => RbcpAckVersion,
        ACK_IN2 => RbcpAckSpiFlashProgrammer,
        ACK_IN3 => RbcpAckSlowControl,
        ACK_IN4 => RbcpAckReadRegister,
        ACK_IN5 => RbcpAckDirectControl,
        ACK_IN6 => RbcpAckStatusRegister,
        ACK_IN7 => RbcpAckAdc,
        ACK_IN8 => RbcpAckSelectableLogic,
        ACK_IN9 => RbcpAckMhtdc,
        ACK_INA => RbcpAckMadc,
        ACK_INB => RbcpAckHVControl,
        ACK_INC => RbcpAckUsrClkOut,
        ACK_IND => RbcpAckTriggerWidth,
        RD_IN1  => RbcpRdVersion,
        RD_IN2  => RbcpRdSpiFlashPromgrammer,
        RD_INA  => RbcpRdMonitorADC,
        RD_OUT  => RBCP_RD,
        ACK_OUT => RBCP_ACK
    );

    Version_0: Version
    generic map(
        G_VERSION_ADDR     => C_VERSION_ADDR,
        G_VERSION          => C_VERSION,
        G_SYNTHESIZED_DATE => C_SYNTHESIZED_DATE
    )
    port map(
        CLK       => SitcpClk,
        RESET     => VersionReset,
        RBCP_ACT  => RBCP_ACT,
        RBCP_ADDR => RBCP_ADDR,
        RBCP_RE   => RBCP_RE,
        RBCP_RD   => RbcpRdVersion,
        RBCP_ACK  => RbcpAckVersion
    );

    DirectControl_0: DirectControl
    generic map(
        G_DIRECT_CONTROL_ADDR => C_DIRECT_CONTROL_ADDR
    )
    port map(
        CLK             => SitcpClk,
        RESET           => DirectControlReset,
        RBCP_ACT        => RBCP_ACT,
        RBCP_ADDR       => RBCP_ADDR,
        RBCP_WE         => RBCP_WE,
        RBCP_WD         => RBCP_WD,
        RBCP_ACK        => RbcpAckDirectControl,
        RAZ_CHN1        => EASIROC1_RAZ_CHN,
        VAL_EVT1        => EASIROC1_VAL_EVT,
        RESET_PA1       => EASIROC1_RESET_PA,
        PWR_ON1         => EASIROC1_PWR_ON,
        SELECT_SC1      => int_EASIROC1_SELECT_SC,
        LOAD_SC1        => EASIROC1_LOAD_SC,
        RSTB_SR1        => EASIROC1_RSTB_SR,
        RSTB_READ1      => RstbReadReadRegister1,
        RAZ_CHN2        => EASIROC2_RAZ_CHN,
        VAL_EVT2        => EASIROC2_VAL_EVT,
        RESET_PA2       => EASIROC2_RESET_PA,
        PWR_ON2         => EASIROC2_PWR_ON,
        SELECT_SC2      => int_EASIROC2_SELECT_SC,
        LOAD_SC2        => EASIROC2_LOAD_SC,
        RSTB_SR2        => EASIROC2_RSTB_SR,
        RSTB_READ2      => RstbReadReadRegister2,
        START_SC_CYCLE1 => StartScCycle1,
        START_SC_CYCLE2 => StartScCycle2
    );

    EASIROC1_SELECT_SC <= int_EASIROC1_SELECT_SC;
    EASIROC2_SELECT_SC <= int_EASIROC2_SElECT_SC;

    GlobalSlowControl_0: GlobalSlowControl
    generic map(
        G_SLOW_CONTROL1_ADDR => C_SLOW_CONTROL1_ADDR,
        G_SLOW_CONTROL2_ADDR => C_SLOW_CONTROL2_ADDR
    )
    port map(
        CLK              => SitcpClk,
        RESET            => SlowControlReset,
        SLOW_CONTROL_CLK => SlowControlClk,
        RBCP_ACT         => RBCP_ACT,
        RBCP_ADDR        => RBCP_ADDR,
        RBCP_WE          => RBCP_WE,
        RBCP_WD          => RBCP_WD,
        RBCP_ACK         => RbcpAckSlowControl,
        START_CYCLE1     => StartScCycle1,
        SELECT_SC1       => int_EASIROC1_SELECT_SC,
        SRIN_SR1         => EASIROC1_SRIN_SR,
        CLK_SR1          => EASIROC1_CLK_SR,
        START_CYCLE2     => StartScCycle2,
        SELECT_SC2       => int_EASIROC2_SELECT_SC,
        SRIN_SR2         => EASIROC2_SRIN_SR,
        CLK_SR2          => EASIROC2_CLK_SR
    );

    GlobalReadRegister_0: GlobalReadRegister
    generic map(
        G_READ_REGISTER1_ADDR => C_READ_REGISTER1_ADDR,
        G_READ_REGISTER2_ADDR => C_READ_REGISTER2_ADDR
    )
    port map(
        CLK               => SitcpClk,
        RESET             => ReadRegisterReset,
        READ_REGISTER_CLK => SlowControlClk,
        RBCP_ACT          => RBCP_ACT,
        RBCP_ADDR         => RBCP_ADDR,
        RBCP_WE           => RBCP_WE,
        RBCP_WD           => RBCP_WD,
        RBCP_ACK          => RbcpAckReadRegister,
        SRIN_READ1        => SrinReadReadRegister1,
        CLK_READ1         => ClkReadReadRegister1,
        SRIN_READ2        => SrinReadReadRegister2,
        CLK_READ2         => ClkReadReadRegister2
    );

    TriggerManager_0: TriggerManager
    port map(
        SITCP_CLK  => SitcpClk,
        ADC_CLK    => AdcClk,
        AD9220_CLK => Ad9220Clk,
        TDC_CLK    => TdcClk,
        SCALER_CLK => ScalerClk,
        FAST_CLK   => FastClk,
        RESET      => TriggerManagerReset,
        HOLD       => IN_FPGA(1),
        L1_TRIGGER => IN_FPGA(4),
        L2_TRIGGER => IN_FPGA(3),
        FAST_CLEAR => IN_FPGA(2),
        BUSY       => Busy,
        TRANSMIT_START    => TransmitStart,
        GATHERER_BUSY     => GathererBusy,
        IS_DAQ_MODE       => DaqMode,
        TCP_OPEN_ACK      => TcpOpenAck,
        ADC_TRIGGER       => AdcTrigger,
        ADC_FAST_CLEAR    => AdcFastClear,
        ADC_BUSY          => AdcBusy,
        COMMON_STOP       => CommonStop,
        TDC_FAST_CLEAR    => TdcFastClear,
        TDC_BUSY          => TdcBusy,
        SCALER_TRIGGER    => ScalerTrigger,
        SCALER_FAST_CLEAR => ScalerFastClear,
        SCALER_BUSY       => ScalerBusy,
        HOLD_OUT1_N       => EASIROC1_HOLDB,
        HOLD_OUT2_N       => EASIROC2_HOLDB
    );

    OUT_FPGA(1) <= Busy;

    StatusRegister_0: StatusRegister
    generic map(
        G_STATUS_REGISTER_ADDR => C_STATUS_REGISTER_ADDR
    )
    port map (
        CLK         => SitcpClk,
        RESET       => StatusRegisterReset,
        RBCP_ACT    => RBCP_ACT,
        RBCP_ADDR   => RBCP_ADDR,
        RBCP_WE     => RBCP_WE,
        RBCP_WD     => RBCP_WD,
        RBCP_ACK    => RbcpAckStatusRegister,
        DAQ_MODE    => DaqMode,
        SEND_ADC    => SendAdc,
        SEND_TDC    => SendTdc,
        SEND_SCALER => SendScaler
    );

    ADC_0: ADC
    generic map (
        G_PEDESTAL_SUPPRESSION_ADDR => C_PEDESTAL_SUPPRESSION_ADDR
    )
    port map (
        SITCP_CLK         => SitcpClk,
        ADC_CLK           => AdcClk,
        AD9220_CLK        => Ad9220Clk,
        RESET             => AdcReset,
        TRIGGER           => AdcTrigger,
        FAST_CLEAR        => AdcFastClear,
        BUSY              => AdcBusy,
        AD9220_CLK_ENABLE => Ad9220ClkEnable,
        CLK_READ1         => ClkReadAdc1,
        RSTB_READ1        => RstbReadAdc1,
        SRIN_READ1        => SrinReadAdc1,
        CLK_READ2         => ClkReadAdc2,
        RSTB_READ2        => RstbReadAdc2,
        SRIN_READ2        => SrinReadAdc2,
        ADC_DATA_HG1      => EASIROC1_ADC_DATA_HG,
        ADC_OTR_HG1       => EASIROC1_ADC_OTR_HG,
        ADC_DATA_LG1      => EASIROC1_ADC_DATA_LG,
        ADC_OTR_LG1       => EASIROC1_ADC_OTR_LG,
        ADC_DATA_HG2      => EASIROC2_ADC_DATA_HG,
        ADC_OTR_HG2       => EASIROC2_ADC_OTR_HG,
        ADC_DATA_LG2      => EASIROC2_ADC_DATA_LG,
        ADC_OTR_LG2       => EASIROC2_ADC_OTR_LG,
        ADC_DOUT_HG1      => AdcDoutHg1,
        ADC_RADDR_HG1     => AdcRaddrHg1,
        ADC_RCOMP_HG1     => AdcRcompHg1,
        ADC_EMPTY_HG1     => AdcEmptyHg1,
        ADC_DOUT_LG1      => AdcDoutLg1,
        ADC_RADDR_LG1     => AdcRaddrLg1,
        ADC_RCOMP_LG1     => AdcRcompLg1,
        ADC_EMPTY_LG1     => AdcEmptyLg1,
        ADC_DOUT_HG2      => AdcDoutHg2,
        ADC_RADDR_HG2     => AdcRaddrHg2,
        ADC_RCOMP_HG2     => AdcRcompHg2,
        ADC_EMPTY_HG2     => AdcEmptyHg2,
        ADC_DOUT_LG2      => AdcDoutLg2,
        ADC_RADDR_LG2     => AdcRaddrLg2,
        ADC_RCOMP_LG2     => AdcRcompLg2,
        ADC_EMPTY_LG2     => AdcEmptyLg2,
        RBCP_ACT          => RBCP_ACT,
        RBCP_ADDR         => RBCP_ADDR,
        RBCP_WE           => RBCP_WE,
        RBCP_WD           => RBCP_WD,
        RBCP_ACK          => RbcpAckAdc
    );

    EASIROC1_ADC_CLK_HG <= Ad9220ClkOut;
    EASIROC1_ADC_CLK_LG <= Ad9220ClkOut;
    EASIROC2_ADC_CLK_HG <= Ad9220ClkOut;
    EASIROC2_ADC_CLK_LG <= Ad9220ClkOut;

    ReadRegisterSelector_1 : ReadRegisterSelector
    port map(
        DAQ_MODE          => DaqMode,
        CLK_READ_ADC      => ClkReadAdc1,
        RSTB_READ_ADC     => RstbReadAdc1,
        SRIN_READ_ADC     => SrinReadAdc1,
        CLK_READ          => ClkReadReadRegister1,
        RSTB_READ         => RstbReadReadRegister1,
        SRIN_READ         => SrinReadReadRegister1,
        EASIROC_CLK_READ  => EASIROC1_CLK_READ,
        EASIROC_RSTB_READ => EASIROC1_RSTB_READ,
        EASIROC_SRIN_READ => EASIROC1_SRIN_READ
    );

    ReadRegisterSelector_2 : ReadRegisterSelector
    port map(
        DAQ_MODE          => DaqMode,
        CLK_READ_ADC      => ClkReadAdc2,
        RSTB_READ_ADC     => RstbReadAdc2,
        SRIN_READ_ADC     => SrinReadAdc2,
        CLK_READ          => ClkReadReadRegister2,
        RSTB_READ         => RstbReadReadRegister2,
        SRIN_READ         => SrinReadReadRegister2,
        EASIROC_CLK_READ  => EASIROC2_CLK_READ,
        EASIROC_RSTB_READ => EASIROC2_RSTB_READ,
        EASIROC_SRIN_READ => EASIROC2_SRIN_READ
    );

    TdcDin <= EASIROC2_TRIGGER & EASIROC1_TRIGGER;
    MHTDC_0: MHTDC
    generic map(
        G_TIME_WINDOW_REGISTER_ADDRESS => C_TIME_WINDOW_REGISTER_ADDRESS
    )
    port map(
        TDC_CLK     => TdcClk,
        CLK_0       => TdcSamplingClk0,
        CLK_90      => TdcSamplingClk90,
        CLK_180     => TdcSamplingClk180,
        CLK_270     => TdcSamplingClk270,
        SITCP_CLK   => SitcpClk,
        RESET       => TdcReset,
        DIN         => TdcDin,
        COMMON_STOP => CommonStop,
        FAST_CLEAR  => TdcFastClear,
        RBCP_ACT    => RBCP_ACT,
        RBCP_ADDR   => RBCP_ADDR,
        RBCP_WE     => RBCP_WE,
        RBCP_WD     => RBCP_WD,
        RBCP_ACK    => RbcpAckMhtdc,
        BUSY        => TdcBusy,
        DOUT_L      => TdcDoutLeading,
        RADDR_L     => TdcRaddrLeading,
        RCOMP_L     => TdcRcompLeading,
        EMPTY_L     => TdcEmptyLeading,
        DOUT_T      => TdcDoutTrailing,
        RADDR_T     => TdcRaddrTrailing,
        RCOMP_T     => TdcRcompTrailing,
        EMPTY_T     => TdcEmptyTrailing
    );

    ScalerTimer_0: ScalerTimer
    port map(
        SCALER_CLK => ScalerClk,
        RESET      => ScalerTimerReset,
        TIMER_1MHZ => ScalerTimer1Mhz,
        TIMER_1KHZ => ScalerTimer1Khz
    );

    DiscriOr_0: DiscriOr
    port map(
        EASIROC1_TRIGGER => EASIROC1_TRIGGER,
        EASIROC2_TRIGGER => EASIROC2_TRIGGER,
        OR32U => Or32U,
        OR32D => OR32D,
        OR64  => OR64
    );

    ScalerDin <= ScalerTimer1Khz & ScalerTimer1Mhz &
                 Or64 & Or32u & Or32d &
                 EASIROC2_TRIGGER & EASIROC1_TRIGGER;

    Scaler_0: Scaler
    port map(
        SCALER_CLK  => ScalerClk,
        SITCP_CLK   => SitcpClk,
        RESET       => ScalerReset,
        RESET_TIMER => ResetExternalScalerTimer,
        DIN         => ScalerDin,
        L1_TRIGGER  => ScalerTrigger,
        FAST_CLEAR  => ScalerFastClear,
        BUSY        => ScalerBusy,
        DOUT        => ScalerDout,
        RADDR       => ScalerRaddr,
        RCOMP       => ScalerRcomp,
        EMPTY       => ScalerEmpty
    );

    GlobalGatherer_0: GlobalGatherer
    port map(
        CLK          => SitcpClk,
        RESET        => GathererReset,
        ADC0_DIN     => AdcDoutHg1,
        ADC0_RADDR   => AdcRaddrHg1,
        ADC0_RCOMP   => AdcRcompHg1,
        ADC0_EMPTY   => AdcEmptyHg1,
        ADC1_DIN     => AdcDoutHg2,
        ADC1_RADDR   => AdcRaddrHg2,
        ADC1_RCOMP   => AdcRcompHg2,
        ADC1_EMPTY   => AdcEmptyHg2,
        ADC2_DIN     => AdcDoutLg1,
        ADC2_RADDR   => AdcRaddrLg1,
        ADC2_RCOMP   => AdcRcompLg1,
        ADC2_EMPTY   => AdcEmptyLg1,
        ADC3_DIN     => AdcDoutLg2,
        ADC3_RADDR   => AdcRaddrLg2,
        ADC3_RCOMP   => AdcRcompLg2,
        ADC3_EMPTY   => AdcEmptyLg2,
        TDC_DIN_L    => TdcDoutLeading,
        TDC_RADDR_L  => TdcRaddrLeading,
        TDC_RCOMP_L  => TdcRcompLeading,
        TDC_EMPTY_L  => TdcEmptyLeading,
        TDC_DIN_T    => TdcDoutTrailing,
        TDC_RADDR_T  => TdcRaddrTrailing,
        TDC_RCOMP_T  => TdcRcompTrailing,
        TDC_EMPTY_T  => TdcEmptyTrailing,
        SCALER_DIN   => ScalerDout,
        SCALER_RADDR => ScalerRaddr,
        SCALER_RCOMP => ScalerRcomp,
        SCALER_EMPTY => ScalerEmpty,
        DOUT         => GathererDout,
        WE           => GathererWe,
        FULL         => SenderFull,
        SEND_ADC     => SendAdc,
        SEND_TDC     => SendTdc,
        SEND_SCALER  => SendScaler,
        TRIGGER      => TransmitStart,
        BUSY         => GathererBusy
    );

    GlobalSender_0: GlobalSender
    port map(
        CLK          => SitcpClk,
        RESET        => SenderReset,
        DIN          => GathererDout,
        WE           => GathererWe,
        FULL         => SenderFull,
        TCP_TX_DATA  => TcpTxData,
        TCP_TX_WR    => TcpTxWr,
        TCP_TX_FULL  => TcpTxFull,
        TCP_OPEN_ACK => TcpOpenAck
    );

    SPI_FLASH_Programmer_0: SPI_FLASH_Programmer
    generic map(
        G_SPI_FLASH_PROGRAMMER_ADDRESS => C_SPI_FLASH_PROGRAMMER_ADDR,
        G_SITCP_CLK_FREQ => real(C_SITCP_CLK_FREQ),
        G_SPI_CLK_FREQ => real(C_SPI_CLK_FREQ)
    )
    port map(
        SPI_CLK   => SpiClk,
        SITCP_CLK => SitcpClk,
        RESET     => SpiFlashProgrammerReset,
        RBCP_ACT  => RBCP_ACT,
        RBCP_ADDR => RBCP_ADDR,
        RBCP_WE   => RBCP_WE,
        RBCP_WD   => RBCP_WD,
        RBCP_RE   => RBCP_RE,
        RBCP_RD   => RbcpRdSpiFlashPromgrammer,
        RBCP_ACK  => RbcpAckSpiFlashProgrammer,
        SPI_SCLK  => SPI_SCLK,
        SPI_SS_N  => SPI_SS_N,
        SPI_MOSI  => SPI_MOSI,
        SPI_MISO  => SPI_MISO,
        RECONFIGURATION_N => PROG_B_ON
    );

    TriggerWidth_0: TriggerWidth 
    generic map(
	  G_TRIGGER_WIDTH_ADDRESS => C_TRIGGER_WIDTH_ADDR
    )
    port map(
          CLK          => SitcpClk,
          RESET        => SitcpReset,
          RBCP_ACT     => RBCP_ACT,
          RBCP_ADDR    => RBCP_ADDR,
          RBCP_WE      => RBCP_WE,
          RBCP_WD      => RBCP_WD,
          RBCP_ACK     => RbcpAckTriggerWidth,
    	  INTERVAL_CLK => TdcClk,
          TRIGGER_IN1  => EASIROC1_TRIGGER,
          TRIGGER_IN2  => EASIROC2_TRIGGER,
          TRIGGER_OUT1 => TRIGGER1,
          TRIGGER_OUT2 => TRIGGER2
    );

    SelectableLogic_0: SelectableLogic
    generic map(
        G_SELECTABLE_LOGIC_ADDRESS => C_SELECTABLE_LOGIC_ADDR
    )
    port map(
        CLK         => SitcpClk,
        RESET       => SelectableLogicReset,
        TRIGGER_IN1 => TRIGGER1,
        TRIGGER_IN2 => TRIGGER2,
        RBCP_ACT    => RBCP_ACT,
        RBCP_ADDR   => RBCP_ADDR,
        RBCP_WE     => RBCP_WE,
        RBCP_WD     => RBCP_WD,
        RBCP_ACK    => RbcpAckSelectableLogic,
        SELECTABLE_LOGIC => OUT_FPGA(2)
    );

    TestChargeInjection_O: TestChargeInjection
    port map(
        CLK  => TdcClk,
        RST  => PWR_RST,
        CAL1 => CAL1,
        CAL2 => CAL2
    );
    
    HVControl_O: HVControl
    generic map(
	    G_HV_CONTROL_ADDR => C_HV_CONTROL_ADDR
    )
    port map(
    	CLK        => SitcpClk,
    	RST        => SitcpReset,
    -- RBCP interface
	RBCP_ACT   => RBCP_ACT,
	RBCP_ADDR  => RBCP_ADDR,
	RBCP_WD    => RBCP_WD,
	RBCP_WE    => RBCP_WE,
	RBCP_ACK   => RbcpAckHVControl,
    -- DAC output
	SDI_DAC    => SDI_DAC,
	SCK_DAC    => SCK_DAC,
	CS_DAC     => CS_DAC,
	HV_EN      => HV_EN,
	-- LED Control
	DOUT_LED   => HV_LED
);

    MADC_O: MADC
    	generic map(
	    G_MONITOR_ADC_ADDR => C_MONITOR_ADC_ADDR,
	    G_READ_MADC_ADDR => C_READ_MADC_ADDR
    	)	    
    	port map(
    	    CLK        =>SitcpClk, 
    	    RST        =>SitcpReset, 
    	    -- RBCP interface
	    RBCP_ACT   => RBCP_ACT,
	    RBCP_ADDR  => RBCP_ADDR,
	    RBCP_WD    => RBCP_WD, 
    	    RBCP_WE    => RBCP_WE,
    	    RBCP_ACK   => RbcpAckMadc, 
    	    RBCP_RD    => RbcpRdMonitorADC, 
    	    RBCP_RE    => RBCP_RE, 
    		-- Monitor ADC
    	    DOUT_MADC  => DOUT_MADC, 
    	    DIN_MADC   => DIN_MADC, 
    	    CS_MADC    => CS_MADC, 
    	    SCK_MADC   => SCK_MADC, 
    	    MUX_EN     => MUX_EN, 
    	    MUX        => MUX
    	);

    user_trigger1 <= DIGITAL_LINE_C1;
    user_trigger2 <= DIGITAL_LINE_C2;
    outbuf1       <= OR32_C1 or OR32_C2;
    OUT_FPGA(4) <= user_trigger1;
    OUT_FPGA(5) <= user_trigger2;
    
    ETH_LED(1) <= 'Z';
    ETH_LED(2) <= 'Z';

    LEDControl_O: LEDControl
    port map(
        CLK        => SitcpClk,
        TcpOpenAck => TcpOpenAck,
        RBCP_ADDR  => RBCP_ADDR, 
        DIN        => HV_LED,
        BUF1       => outbuf1, 
        Busy       => Busy, 
        LED1       => LED(1),      
        LED2       => LED(2),      
        LED3       => LED(3),      
        LED4       => LED(4),      
        LED5       => LED(5),      
        LED6       => LED(6),      
        LED7       => LED(7),      
        LED8       => LED(8)
    );

    UsrClkOut_O: UsrClkOut 
    	generic map(
    	    G_USER_OUTPUT_ADDR => C_USER_OUTPUT_ADDR
    	)	    	
    	port map(
    	    CLK_25M   => SitcpClk,
    	    RST       => SitcpReset,
    	    
    	    RBCP_ACT  => RBCP_ACT,
    	    RBCP_ADDR => RBCP_ADDR,
    	    RBCP_WD   => RBCP_WD,
    	    RBCP_WE   => RBCP_WE, 
    	    RBCP_ACK  => RBCPAckUsrClkOut,

    	    CLK_500M  => FastClk,
    	    CLK_125M  => TdcClk,
    	    CLK_3M    => Ad9220Clk,
	    DOUT      => OUT_FPGA(3)
    	);



end RTL;
