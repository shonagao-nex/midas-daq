--------------------------------------------------------------------------------
--! @file   FineCounter.vhd
--! @brief  Decoder of fine counter for MHTDC
--! @author Takehiro Shiozaki
--! @date   2014-06-06
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;

entity FineCounterDecoder is
    port (
        CLK_0 : in std_logic; -- 250MHz
        TDC_CLK : in std_logic;   -- 125MHz

        DIN : in std_logic_vector(3 downto 0);

        SEMI_FINE_COUNT : out std_logic;
        FINE_COUNT : out std_logic_vector(1 downto 0);
        HIT_FIND : out std_logic
    );
end FineCounterDecoder;

architecture RTL of FineCounterDecoder is
    signal Stage0 : std_logic_vector(3 downto 0);
    signal Stage1 : std_logic_vector(3 downto 0);

    signal SynchStage0 : std_logic_vector(3 downto 0);
    signal SynchStage1 : std_logic_vector(3 downto 0);

    signal PreviousSynchStage0 : std_logic_vector(3 downto 0);
begin

    process(CLK_0)
    begin
        if(CLK_0'event and CLK_0 = '1') then
            Stage0 <= DIN;
            Stage1 <= Stage0;
        end if;
    end process;

    process(TDC_CLK)
    begin
        if(TDC_CLK'event and TDC_CLK = '1') then
            SynchStage0 <= Stage0;
            SynchStage1 <= Stage1;
            PreviousSynchStage0 <= SynchStage0;
        end if;
    end process;

    process(SynchStage0, SynchStage1, PreviousSynchStage0)
    begin
        if(SynchStage1 = "1111") then
            case PreviousSynchStage0 is
                when "0000" =>
                    FINE_COUNT <= "11";
                when "1000" =>
                    FINE_COUNT <= "10";
                when "1100" =>
                    FINE_COUNT <= "01";
                when "1110" =>
                    FINE_COUNT <= "00";
                when others =>
                    FINE_COUNT <= (others => 'X');
            end case;
        else
            case SynchStage1 is
                when "0000" =>
                    FINE_COUNT <= "11";
                when "1000" =>
                    FINE_COUNT <= "10";
                when "1100" =>
                    FINE_COUNT <= "01";
                when "1110" =>
                    FINE_COUNT <= "00";
                when others =>
                    FINE_COUNT <= (others => 'X');
            end case;
        end if;
    end process;

    process(SynchStage1)
    begin
        if(SynchStage1 = "1111") then
            SEMI_FINE_COUNT <= '0';
        else
            SEMI_FINE_COUNT <= '1';
        end if;
    end process;

    process(SynchStage0, SynchStage1, PreviousSynchStage0)
    begin
        if(SynchStage0 = "1111" and SynchStage1 /= "1111") then
            HIT_FIND <= '1';
        elsif(SynchStage1 = "1111" and PreviousSynchStage0 /= "1111") then
            HIT_FIND <= '1';
        else
            HIT_FIND <= '0';
        end if;
    end process;
end RTL;
