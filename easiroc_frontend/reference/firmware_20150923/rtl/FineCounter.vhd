--------------------------------------------------------------------------------
--! @file   FineCounter.vhd
--! @brief  Fine counter for MHTDC
--! @author Takehiro Shiozaki
--! @date   2014-06-06
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;

entity FineCounter is
    port (
        CLK_0 : in std_logic;
        CLK_90 : in std_logic;
        CLK_180 : in std_logic;
        CLK_270 : in std_logic;

        DIN : in std_logic;
        DOUT : out std_logic_vector(3 downto 0)
    );
end FineCounter;

architecture RTL of FineCounter is
    signal Stage0 : std_logic_vector(3 downto 0);
    signal Stage1 : std_logic_vector(3 downto 0);
    signal DelayedStage1 : std_logic_vector(2 downto 0);
begin

    process(CLK_0)
    begin
        if(CLK_0'event and CLK_0 = '1') then
            Stage0(0) <= DIN;
        end if;
    end process;

    process(CLK_90)
    begin
        if(CLK_90'event and CLK_90 = '1') then
            Stage0(1) <= DIN;
        end if;
    end process;

    process(CLK_180)
    begin
        if(CLK_180'event and CLK_180 = '1') then
            Stage0(2) <= DIN;
        end if;
    end process;

    process(CLK_270)
    begin
        if(CLK_270'event and CLK_270 = '1') then
            Stage0(3) <= DIN;
        end if;
    end process;

    process(CLK_0)
    begin
        if(CLK_0'event and CLK_0 ='1') then
            Stage1 <= Stage0;
        end if;
    end process;

    process(CLK_0)
    begin
        if(CLK_0'event and CLK_0 = '1') then
            DelayedStage1 <= Stage1(2 downto 0);
        end if;
    end process;

    DOUT <= Stage1(3) & DelayedStage1;
end RTL;
