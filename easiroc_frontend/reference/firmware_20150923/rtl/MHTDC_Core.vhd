--------------------------------------------------------------------------------
--! @file   MHTDC_Core.vhd
--! @brief  read time from Counter and write to Double buffer
--! @author Takehiro Shiozaki
--! @date   2014-06-10
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;
use ieee.std_logic_arith.all;

library work;
use work.MHTDC_DataType.all;

entity MHTDC_Core is
    generic (
        G_TIME_WINDOW_REGISTER_ADDRESS : std_logic_vector(31 downto 0) := X"00000000"
    );
    port(
        TDC_CLK : in std_logic; -- 125MHz
        CLK_0 : in std_logic;   -- 250MHz 0degree
        CLK_90 : in std_logic;  -- 250MHz 90degree
        CLK_180 : in std_logic; -- 250MHz 180degree
        CLK_270 : in std_logic; -- 250MHz 270degree
        RESET : in std_logic;

        DIN : in std_logic_vector(63 downto 0);
        COMMON_STOP : in std_logic;
        FAST_CLEAR : in std_logic;

        -- RBCP Interface
        SITCP_CLK : in std_logic;
        RBCP_ACT : in std_logic;
        RBCP_ADDR : in std_logic_vector(31 downto 0);
        RBCP_WE : in std_logic;
        RBCP_WD : in std_logic_vector(7 downto 0);
        RBCP_ACK : out std_logic;

        -- Eventbuffer interface(Leading)
        DOUT_L : out std_logic_vector(19 downto 0);
        ADDR_L : out std_logic_vector(10 downto 0);
        WE_L : out std_logic;
        FULL_L : in std_logic;
        WCOMP_L : out std_logic;
        DEC_WPTR_L : out std_logic;

        -- Eventbuffer interface(Trailing)
        DOUT_T : out std_logic_vector(19 downto 0);
        ADDR_T : out std_logic_vector(10 downto 0);
        WE_T : out std_logic;
        FULL_T : in std_logic;
        WCOMP_T : out std_logic;
        DEC_WPTR_T : out std_logic;

        BUSY : out std_logic
    );
end MHTDC_Core;

architecture RTL of MHTDC_Core is
    component MHTDC_Counter is
        port(
            CLK_0 : in std_logic;   -- 250MHz 0degree
            CLK_90 : in std_logic;  -- 250MHz 90degree
            CLK_180 : in std_logic; -- 250MHz 180degree
            CLK_270 : in std_logic; -- 250MHz 270degree
            TDC_CLK : in std_logic; -- 125MHz

            DIN : in std_logic;
            COARSE_COUNT : in std_logic_vector(42 downto 0);
            COUNT : out std_logic_vector(45 downto 0);
            HIT_FIND : out std_logic
        );
    end component;

    component MHTDC_ChannelBuffer is
        port(
            TDC_CLK : in std_logic;
            RESET : in std_logic;
            DIN : in std_logic_vector(45 downto 0);
            WE : in std_logic;
            DOUT : out std_logic_vector(45 downto 0);
            RE : in std_logic;
            EMPTY : out std_logic;
            CLEAR : in std_logic
        );
    end component;

    component MHTDC_Builder is
        generic(
            G_LT : std_logic
        );
        port(
            TDC_CLK : in std_logic;
            RESET : in std_logic;

            DIN : in MHTDC_CounterArray;
            RE : out std_logic_vector(63 downto 0);
            EMPTY : in std_logic_vector(63 downto 0);
            CLEAR : out std_logic_vector(63 downto 0);

            -- Commom Stop interface
            COMMON_STOP : in std_logic;
            COMMON_STOP_COUNT : in std_logic_vector(45 downto 0);
            FAST_CLEAR : in std_logic;

            -- Double buffer interface
            DOUT : out std_logic_vector(19 downto 0);
            WADDR : out std_logic_vector(10 downto 0);
            WE : out std_logic;
            FULL : in std_logic;
            WCOMP : out std_logic;
            DEC_WPTR : out std_logic;

            -- Control interface
            TIME_WINDOW : in std_logic_vector(11 downto 0);
            BUSY : out std_logic
        );
    end component;

    component CommonStopManager is
        port(
            CLK_0 : in std_logic;   -- 250MHz 0degree
            CLK_90 : in std_logic;  -- 250MHz 90degree
            CLK_180 : in std_logic; -- 250MHz 180degree
            CLK_270 : in std_logic; -- 250MHz 270degree
            TDC_CLK : in std_logic; -- 125MHz

            COMMON_STOP : in std_logic;
            COARSE_COUNT : in std_logic_vector(42 downto 0);

            COMMON_STOP_OUT : out std_logic;
            COMMON_STOP_COUNT : out std_logic_vector(45 downto 0)
        );
    end component;

    component TimeWindowRegister is
        generic (
            G_TIME_WINDOW_REGISTER_ADDRESS : std_logic_vector(31 downto 0) := X"00000000"
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

            TIME_WINDOW : out std_logic_vector(11 downto 0)
        );
    end component;

    -- Leading
    signal HitFindLeading : std_logic_vector(63 downto 0);
    signal MaskedHitFindLeading : std_logic_vector(63 downto 0);
    signal CountLeading : MHTDC_CounterArray;
    signal ChannelBufferDoutLeading : MHTDC_CounterArray;
    signal ChannelBufferEmptyLeading : std_logic_vector(63 downto 0);
    signal BuilderReLeading : std_logic_vector(63 downto 0);
    signal BuilderClearLeading : std_logic_vector(63 downto 0);

    -- Trailing
    signal Din_N : std_logic_vector(63 downto 0);
    signal HitFindTrailing : std_logic_vector(63 downto 0);
    signal MaskedHitFindTrailing : std_logic_vector(63 downto 0);
    signal CountTrailing : MHTDC_CounterArray;
    signal ChannelBufferDoutTrailing : MHTDC_CounterArray;
    signal ChannelBufferEmptyTrailing : std_logic_vector(63 downto 0);
    signal BuilderReTrailing : std_logic_vector(63 downto 0);
    signal BuilderClearTrailing : std_logic_vector(63 downto 0);

    -- CommonStop
    signal CommonStop : std_logic;
    signal CommonStopCount : std_logic_vector(45 downto 0);

    -- CoarseCount
    signal CoarseCount : std_logic_vector(42 downto 0);
    signal CoarseCountLeading : std_logic_vector(42 downto 0);
    signal CoarseCountTrailing : std_logic_vector(42 downto 0);
    signal CoarseCountCommonStop : std_logic_vector(42 downto 0);

    -- Control
    signal BusyLeading : std_logic;
    signal BusyTrailing : std_logic;
    signal int_BUSY : std_logic;
    signal TimeWindow : std_logic_vector(11 downto 0);
begin
    -- Leading
    COUNTER_LEADING_GENERATE: for i in 0 to 63 generate
        MHTDC_Counter_Leading: MHTDC_Counter
        port map(
            CLK_0 => CLK_0,
            CLK_90 => CLK_90,
            CLK_180 => CLK_180,
            CLK_270 => CLK_270,
            TDC_CLK => TDC_CLK,
            DIN => DIN(i),
            COARSE_COUNT => CoarseCountLeading,
            COUNT => CountLeading(i),
            HIT_FIND => HitFindLeading(i)
        );
    end generate COUNTER_LEADING_GENERATE;

    MASKED_HIT_FIND_LEADING_GENERATE: for i in 0 to 63 generate
        MaskedHitFindLeading(i) <= HitFindLeading(i) and (not int_BUSY);
    end generate MASKED_HIT_FIND_LEADING_GENERATE;

    CHANNEL_BUFFER_LEADING_GENERATE: for i in 0 to 63 generate
        MHTDC_ChannelBuffer_Leading: MHTDC_ChannelBuffer
        port map(
            TDC_CLK => TDC_CLK,
            RESET => RESET,
            DIN => CountLeading(i),
            WE => MaskedHitFindLeading(i),
            DOUT => ChannelBufferDoutLeading(i),
            RE => BuilderReLeading(i),
            EMPTY => ChannelBufferEmptyLeading(i),
            CLEAR => BuilderClearLeading(i)
        );
    end generate CHANNEL_BUFFER_LEADING_GENERATE;

    MHTDC_Builder_Leading: MHTDC_Builder
    generic map(
        G_LT => '1'
    )
    port map(
        TDC_CLK => TDC_CLK,
        RESET => RESET,
        DIN => ChannelBufferDoutLeading,
        RE => BuilderReLeading,
        EMPTY => ChannelBufferEmptyLeading,
        CLEAR => BuilderClearLeading,
        COMMON_STOP => CommonStop,
        COMMON_STOP_COUNT => CommonStopCount,
        FAST_CLEAR => FAST_CLEAR,
        DOUT => DOUT_L,
        WADDR => ADDR_L,
        WE => WE_L,
        FULL => FULL_L,
        WCOMP => WCOMP_L,
        DEC_WPTR => DEC_WPTR_L,
        TIME_WINDOW => TimeWindow,
        BUSY => BusyLeading
    );

    -- Trailing
    DIN_N_GENERATE: for i in 0 to 63 generate
        Din_N(i) <= not Din(i);
    end generate DIN_N_GENERATE;

    COUNTER_TRAILING_GENERATE: for i in 0 to 63 generate
        MHTDC_Counter_Trailing: MHTDC_Counter
        port map(
            CLK_0 => CLK_0,
            CLK_90 => CLK_90,
            CLK_180 => CLK_180,
            CLK_270 => CLK_270,
            TDC_CLK => TDC_CLK,
            DIN => Din_N(i),
            COARSE_COUNT => CoarseCountTrailing,
            COUNT => CountTrailing(i),
            HIT_FIND => HitFindTrailing(i)
        );
    end generate COUNTER_TRAILING_GENERATE;

    MASKED_HIT_FIND_TRAILING_GENERATE: for i in 0 to 63 generate
        MaskedHitFindTrailing(i) <= HitFindTrailing(i) and (not int_BUSY);
    end generate MASKED_HIT_FIND_TRAILING_GENERATE;

    CHANNEL_BUFFER_TRAILING_GENERATE: for i in 0 to 63 generate
        MHTDC_ChannelBuffer_Trailing: MHTDC_ChannelBuffer
        port map(
            TDC_CLK => TDC_CLK,
            RESET => RESET,
            DIN => CountTrailing(i),
            WE => MaskedHitFindTrailing(i),
            DOUT => ChannelBufferDoutTrailing(i),
            RE => BuilderReTrailing(i),
            EMPTY => ChannelBufferEmptyTrailing(i),
            CLEAR => BuilderClearTrailing(i)
        );
    end generate CHANNEL_BUFFER_TRAILING_GENERATE;

    MHTDC_Builder_Trailing: MHTDC_Builder
    generic map(
        G_LT => '0'
    )
    port map(
        TDC_CLK => TDC_CLK,
        RESET => RESET,
        DIN => ChannelBufferDoutTrailing,
        RE => BuilderReTrailing,
        EMPTY => ChannelBufferEmptyTrailing,
        CLEAR => BuilderClearTrailing,
        COMMON_STOP => CommonStop,
        COMMON_STOP_COUNT => CommonStopCount,
        FAST_CLEAR => FAST_CLEAR,
        DOUT => DOUT_T,
        WADDR => ADDR_T,
        WE => WE_T,
        FULL => FULL_T,
        WCOMP => WCOMP_T,
        DEC_WPTR => DEC_WPTR_T,
        TIME_WINDOW => TimeWindow,
        BUSY => BusyTrailing
    );

    -- CommonStop
    CommonStopManager_0: CommonStopManager
    port map(
        CLK_0 => CLK_0,
        CLK_90 => CLK_90,
        CLK_180 => CLK_180,
        CLK_270 => CLK_270,
        TDC_CLK => TDC_CLK,
        COMMON_STOP => COMMON_STOP,
        COARSE_COUNT => CoarseCountCommonStop,
        COMMON_STOP_OUT => CommonStop,
        COMMON_STOP_COUNT => CommonStopCount
    );

    -- CoarseCount
    process(TDC_CLK)
    begin
        if(TDC_CLK'event and TDC_CLK = '1') then
            if(RESET = '1') then
                CoarseCount <= (others => '0');
            else
                CoarseCount <= CoarseCount + 1;
            end if;
        end if;
    end process;

    process(TDC_CLK)
    begin
        if(TDC_CLK'event and TDC_CLK = '1') then
            CoarseCountLeading <= CoarseCount;
            CoarseCountTrailing <= CoarseCount;
            CoarseCountCommonStop <= CoarseCount;
        end if;
    end process;

    -- Timewindow
    TimeWindowRegister_0 : TimeWindowRegister
    generic map(
        G_TIME_WINDOW_REGISTER_ADDRESS => G_TIME_WINDOW_REGISTER_ADDRESS
    )
    port map(
        CLK => SITCP_CLK,
        RESET => RESET,
        RBCP_ACT => RBCP_ACT,
        RBCP_ADDR => RBCP_ADDR,
        RBCP_WE => RBCP_WE,
        RBCP_WD => RBCP_WD,
        RBCP_ACK => RBCP_ACK,
        TIME_WINDOW => TimeWindow
    );

    -- Control
    int_BUSY <= BusyLeading or BusyTrailing;
    BUSY <= int_BUSY;
end RTL;
