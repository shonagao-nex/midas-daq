--------------------------------------------------------------------------------
--! @file   ReadRegisterSelector.vhd
--! @brief  select CLK_READ, RSTB_READ, and SRIN_READ signals
--! @author Takehiro Shiozaki
--! @date   2015-02-17
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;

entity ReadRegisterSelector is
    port (
        DAQ_MODE : in std_logic;
        CLK_READ_ADC : in std_logic;
        RSTB_READ_ADC : in std_logic;
        SRIN_READ_ADC : in std_logic;
        CLK_READ : in std_logic;
        RSTB_READ : in std_logic;
        SRIN_READ : in std_logic;
        EASIROC_CLK_READ : out std_logic;
        EASIROC_RSTB_READ : out std_logic;
        EASIROC_SRIN_READ : out std_logic
    );
end ReadRegisterSelector;

architecture RTL of ReadRegisterSelector is
begin
    process(DAQ_MODE, CLK_READ_ADC, RSTB_READ_ADC, SRIN_READ_ADC,
            CLK_READ, RSTB_READ, SRIN_READ)
    begin
        if(DAQ_MODE = '0') then
            EASIROC_CLK_READ  <= CLK_READ;
            EASIROC_RSTB_READ <= RSTB_READ;
            EASIROC_SRIN_READ <= SRIN_READ;
        else
            EASIROC_CLK_READ  <= CLK_READ_ADC;
            EASIROC_RSTB_READ <= RSTB_READ_ADC;
            EASIROC_SRIN_READ <= SRIN_READ_ADC;
        end if;
    end process;
end RTL;
