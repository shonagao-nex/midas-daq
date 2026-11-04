--------------------------------------------------------------------------------
--! @file   HoldExpander.vhd
--! @brief  Expand Hold signal
--! @author Takehiro Shiozaki
--! @date   2014-06-18
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;

entity HoldExpander is
    port(
        FAST_CLK : in std_logic;
        RESET : in  std_logic;

        HOLD_IN : in std_logic;
        HOLD_OUT1_N : out std_logic;
        HOLD_OUT2_N : out std_logic;

        EXTERNAL_RESET_HOLD : in std_logic;
        IS_EXTERNAL_RESET_HOLD : in std_logic
    );
end HoldExpander;

architecture RTL of HoldExpander is

    component EdgeDetector
        port(
            CLK : in std_logic;
            RESET : in std_logic;
            DIN : in std_logic;
            DOUT : out std_logic
        );
    end component;

    component Delayer
        generic(
            G_CLK : integer
        );
        port(
            CLK : in  std_logic;
            DIN : in  std_logic;
            DOUT : out  std_logic
        );
    end component;

    signal HoldOut : std_logic;

    signal ResetHold : std_logic;
    signal ResetHoldSelf : std_logic;

    signal HoldOutEdge : std_logic;
begin

    process(FAST_CLK)
    begin
        if(FAST_CLK'event and FAST_CLK = '1') then
            if(IS_EXTERNAL_RESET_HOLD = '1') then
                ResetHold <= EXTERNAL_RESET_HOLD;
            else
                ResetHold <= ResetHoldSelf;
            end if;
        end if;
    end process;

    process(FAST_CLK)
    begin
        if(FAST_CLK'event and FAST_CLK = '1') then
            if(RESET = '1' or ResetHold = '1') then
                HoldOut <= '0';
            elsif(HOLD_IN = '1') then
                HoldOut <= '1';
            end if;
        end if;
    end process;

    HOLD_OUT1_N <= not HoldOut;
    HOLD_OUT2_N <= not HoldOut;

    EdgeDetector_HOLD: EdgeDetector
    port map(
        CLK => FAST_CLK,
        RESET => RESET,
        DIN => HoldOut,
        DOUT => HoldOutEdge
    );

    Delayer_0: Delayer
    generic map(
            G_CLK => 1000  -- 2us * 500MHz
    )
    port map(
          CLK => FAST_CLK,
          DIN => HoldOutEdge,
          DOUT => ResetHoldSelf
    );

end RTL;

