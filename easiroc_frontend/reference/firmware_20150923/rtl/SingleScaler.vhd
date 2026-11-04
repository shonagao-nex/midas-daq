--------------------------------------------------------------------------------
--! @file   SingleScaler.vhd
--! @brief  one Scaler
--! @author Takehiro Shiozaki
--! @date   2014-08-18
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;

entity SingleScaler is
    generic (
        G_BITS : integer
    );
    port (
        CLK : in std_logic;
        RESET : in std_logic;
        DIN : in std_logic;
        DOUT : out std_logic_vector(G_BITS - 1 downto 0);
        OVERFLOW : out std_logic
    );
end SingleScaler;

architecture RTL of SingleScaler is
    component EdgeDetector is
        port (
            CLK : in  std_logic;
            RESET : in  std_logic;
            DIN : in  std_logic;
            DOUT : out  std_logic
        );
    end component;

    signal DinEdge : std_logic;
    signal Count : std_logic_vector(G_BITS - 1 downto 0);
    constant C_ALL_ONE : std_logic_vector(G_BITS - 1 downto 0) := (others => '1');
begin
    EdgeDetector_0 : EdgeDetector
    port map(
        CLK => CLK,
        RESET => RESET,
        DIN => DIN,
        DOUT => DinEdge
    );

    process(CLK)
    begin
        if(CLK'event and CLK = '1') then
            if(RESET = '1') then
                Count <= (others => '0');
            else
                if(DinEdge = '1') then
                    Count <= Count + 1;
                end if;
            end if;
        end if;
    end process;

    process(CLK)
    begin
        if(CLK'event and CLK = '1') then
            if(RESET = '1') then
                OVERFLOW <= '0';
            else
                if(Count = C_ALL_ONE and DinEdge = '1') then
                    OVERFLOW <= '1';
                end if;
            end if;
        end if;
    end process;

    DOUT <= Count;
end RTL;
