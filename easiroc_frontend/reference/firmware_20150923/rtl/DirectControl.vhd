--------------------------------------------------------------------------------
--! @file   DirectControl.vhd
--! @brief  Drive EASIROC control signals directly
--! @author Naruhiro Chikuma
--! @date   2015-9-3
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use work.Asynch.all;

entity DirectControl is
    generic (
        G_DIRECT_CONTROL_ADDR : std_logic_vector(31 downto 0) := X"00000000"
    );
    port (
        CLK : in  std_logic;
        RESET : in  std_logic;

        -- RBCP Interface
        RBCP_ACT : in std_logic;
        RBCP_ADDR : in std_logic_vector(31 downto 0);
        RBCP_WE : in std_logic;
        RBCP_WD : in std_logic_vector(7 downto 0);
        RBCP_ACK : out std_logic;

        -- Direct Control Interface
        -- EASIROC1
        RAZ_CHN1 : out std_logic;
        VAL_EVT1 : out std_logic;
        RESET_PA1 : out std_logic;
        PWR_ON1 : out std_logic;
        SELECT_SC1 : out std_logic;
        LOAD_SC1 : out std_logic;
        RSTB_SR1 : out std_logic;
        RSTB_READ1 : out std_logic;

        -- EASIROC2
        RAZ_CHN2 : out std_logic;
        VAL_EVT2 : out std_logic;
        RESET_PA2 : out std_logic;
        PWR_ON2 : out std_logic;
        SELECT_SC2 : out std_logic;
        LOAD_SC2 : out std_logic;
        RSTB_SR2 : out std_logic;
        RSTB_READ2 : out std_logic;

        -- SlowControl Control Interface
        START_SC_CYCLE1 : out std_logic;
        START_SC_CYCLE2 : out std_logic

    );
    end DirectControl;

architecture RTL of DirectControl is

    constant C_DIRECT_CONTROL_ADDR : std_logic_vector(31 downto 0) := G_DIRECT_CONTROL_ADDR;

    constant C_RAZ_CHN1_BIT        : integer := 0;
    constant C_VAL_EVT1_BIT        : integer := 1;
    constant C_RESET_PA1_BIT       : integer := 2;
    constant C_PWR_ON1_BIT         : integer := 3;
    constant C_SELECT_SC1_BIT      : integer := 4;
    constant C_LOAD_SC1_BIT        : integer := 5;
    constant C_RSTB_SR1_BIT        : integer := 6;
    constant C_RSTB_READ1_BIT      : integer := 7;

    constant C_RAZ_CHN2_BIT        : integer := 0;
    constant C_VAL_EVT2_BIT        : integer := 1;
    constant C_RESET_PA2_BIT       : integer := 2;
    constant C_PWR_ON2_BIT         : integer := 3;
    constant C_SELECT_SC2_BIT      : integer := 4;
    constant C_LOAD_SC2_BIT        : integer := 5;
    constant C_RSTB_SR2_BIT        : integer := 6;
    constant C_RSTB_READ2_BIT      : integer := 7;

    constant C_START_SC_CYCLE1_BIT : integer := 0;
    constant C_START_SC_CYCLE2_BIT : integer := 1;

    component RBCP_Receiver
        generic (
            G_ADDR : std_logic_vector(31 downto 0);
            G_LEN : integer;
            G_ADDR_WIDTH : integer
        );
        port(
            CLK : in std_logic;
            RESET : in std_logic;
            RBCP_ACT : in std_logic;
            RBCP_ADDR : in std_logic_vector(31 downto 0);
            RBCP_WE : in std_logic;
            RBCP_WD : in std_logic_vector(7 downto 0);
            RBCP_ACK : out std_logic;
            ADDR : out std_logic_vector(G_ADDR_WIDTH -1 downto 0);
            WE : out std_logic;
            WD : out std_logic_vector(7 downto 0)
        );
    end component;

    component DFF_1Shot
        port(
            CLK : in std_logic;
            RESET : in std_logic;
            D : in std_logic;
            EN : in std_logic;
            Q : out std_logic
        );
    end component;

    component PulseExtender
        generic(
            G_WIDTH : integer
        );
        port(
            CLK : in std_logic;
            RESET : in std_logic;
            DIN : in std_logic;
            DOUT : out std_logic
        );
    end component;

    signal Addr : std_logic_vector(1 downto 0);
    signal We : std_logic;
    signal Wd : std_logic_vector(7 downto 0);

    signal DirectControlRegister0 : std_logic_vector(7 downto 0);
    signal DirectControlRegister1 : std_logic_vector(7 downto 0);
    signal DirectControlRegister2 : std_logic_vector(7 downto 0);

    signal DirectControlRegisterWe0 : std_logic;
    signal DirectControlRegisterWe1 : std_logic;
    signal DirectControlRegisterWe2 : std_logic;
    signal DelayedDirectControlRegisterWe2 : std_logic;


    signal StartScCycle1Shot1 : std_logic;
    signal StartScCycle1Shot2 : std_logic;

begin
    RBCP_Receiver_0: RBCP_Receiver
    generic map(
        G_ADDR => C_DIRECT_CONTROL_ADDR,
        G_LEN => 3,
        G_ADDR_WIDTH => 2
    )
    port map(
        CLK => CLK,
        RESET => RESET,
        RBCP_ACT => RBCP_ACT,
        RBCP_ADDR => RBCP_ADDR,
        RBCP_WE => RBCP_WE,
        RBCP_WD => RBCP_WD,
        RBCP_ACK => RBCP_ACK,
        ADDR => Addr,
        WE => We,
        WD => Wd
    );

    DFF_1Shot_START_SC_CYCLE1: DFF_1Shot
    port map(
        CLK => CLK,
        RESET => RESET,
        D => DirectControlRegister2(C_START_SC_CYCLE1_BIT),
        EN => DelayedDirectControlRegisterWe2,
        Q => StartScCycle1Shot1
    );

    DFF_1Shot_START_SC_CYCLE2: DFF_1Shot
    port map(
        CLK => CLK,
        RESET => RESET,
        D => DirectControlRegister2(C_START_SC_CYCLE2_BIT),
        EN => DelayedDirectControlRegisterWe2,
        Q => StartScCycle1Shot2
    );

    PulseExtender_START_SC_CYCLE1: PulseExtender
    generic map(
        G_WIDTH => C_SITCP_CLK_TO_SLOWCONTROL_CLK
    )
    port map(
        CLK => CLK,
        RESET => RESET,
        DIN => StartScCycle1Shot1,
        DOUT => START_SC_CYCLE1
    );

    PulseExtender_START_SC_CYCLE2: PulseExtender
    generic map(
        G_WIDTH => C_SITCP_CLK_TO_SLOWCONTROL_CLK
    )
    port map(
        CLK => CLK,
        RESET => RESET,
        DIN => StartScCycle1Shot2,
        DOUT => START_SC_CYCLE2
    );

    process(RESET, CLK)
    begin
        if(RESET = '1') then
            DelayedDirectControlRegisterWe2 <= '0';
        elsif(CLK'event and CLK = '1') then
            DelayedDirectControlRegisterWe2 <= DirectControlRegisterWe2;
        end if;
    end process;

    process(We, Addr)
    begin
        if(We = '1') then
            case(Addr) is
                when "00" =>
                    DirectControlRegisterWe0 <= '1';
                    DirectControlRegisterWe1 <= '0';
                    DirectControlRegisterWe2 <= '0';
                when "01" =>
                    DirectControlRegisterWe0 <= '0';
                    DirectControlRegisterWe1 <= '1';
                    DirectControlRegisterWe2 <= '0';
                when "10" =>
                    DirectControlRegisterWe0 <= '0';
                    DirectControlRegisterWe1 <= '0';
                    DirectControlRegisterWe2 <= '1';
                when others =>
                    DirectControlRegisterWe0 <= '0';
                    DirectControlRegisterWe1 <= '0';
                    DirectControlRegisterWe2 <= '0';
            end case;
        else
            DirectControlRegisterWe0 <= '0';
            DirectControlRegisterWe1 <= '0';
            DirectControlRegisterWe2 <= '0';
        end if;
    end process;

    process(RESET, CLK)
    begin
        if(RESET = '1') then
            DirectControlRegister0 <= (others => '0');
            DirectControlRegister1 <= (others => '0');
            DirectControlRegister2 <= (others => '0');
        elsif(CLK'event and CLK = '1') then
            if(DirectControlRegisterWe0 = '1') then
                DirectControlRegister0 <= Wd;
            end if;

            if(DirectControlRegisterWe1 = '1') then
                DirectControlRegister1 <= Wd;
            end if;

            if(DirectControlRegisterWe2 = '1') then
                DirectControlRegister2 <= Wd;
            end if;
        end if;
    end process;

    RAZ_CHN1        <= DirectControlRegister0(C_RAZ_CHN1_BIT);
    VAL_EVT1        <= DirectControlRegister0(C_VAL_EVT1_BIT);
    RESET_PA1       <= DirectControlRegister0(C_RESET_PA1_BIT);
    PWR_ON1         <= DirectControlRegister0(C_PWR_ON1_BIT);
    SELECT_SC1      <= DirectControlRegister0(C_SELECT_SC1_BIT);
    LOAD_SC1        <= DirectControlRegister0(C_LOAD_SC1_BIT);
    RSTB_SR1        <= DirectControlRegister0(C_RSTB_SR1_BIT);
    RSTB_READ1      <= DirectControlRegister0(C_RSTB_READ1_BIT);

    RAZ_CHN2        <= DirectControlRegister1(C_RAZ_CHN2_BIT);
    VAL_EVT2        <= DirectControlRegister1(C_VAL_EVT2_BIT);
    RESET_PA2       <= DirectControlRegister1(C_RESET_PA2_BIT);
    PWR_ON2         <= DirectControlRegister1(C_PWR_ON2_BIT);
    SELECT_SC2      <= DirectControlRegister1(C_SELECT_SC2_BIT);
    LOAD_SC2        <= DirectControlRegister1(C_LOAD_SC2_BIT);
    RSTB_SR2        <= DirectControlRegister1(C_RSTB_SR2_BIT);
    RSTB_READ2      <= DirectControlRegister1(C_RSTB_READ2_BIT);

end RTL;
