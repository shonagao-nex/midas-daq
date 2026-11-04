--------------------------------------------------------------------------------
--! @file   RegisterAddress.vhd
--! @brief  RegisterAddress definition
--! @author Naruhiro Chikuma
--! @date   2014-09-06
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;

package RegisterAddress is

constant C_DIRECT_CONTROL_ADDR          : std_logic_vector(31 downto 0) := X"00000000";
constant C_SLOW_CONTROL1_ADDR           : std_logic_vector(31 downto 0) := X"00000003";
constant C_READ_REGISTER1_ADDR          : std_logic_vector(31 downto 0) := X"0000003C";
constant C_SLOW_CONTROL2_ADDR           : std_logic_vector(31 downto 0) := X"0000003D";
constant C_READ_REGISTER2_ADDR          : std_logic_vector(31 downto 0) := X"00000076";
constant C_STATUS_REGISTER_ADDR         : std_logic_vector(31 downto 0) := X"00000077";
constant C_SELECTABLE_LOGIC_ADDR        : std_logic_vector(31 downto 0) := X"00000078";
constant C_TRIGGER_WIDTH_ADDR           : std_logic_vector(31 downto 0) := X"00000088";
constant C_TIME_WINDOW_REGISTER_ADDRESS : std_logic_vector(31 downto 0) := X"00000100";
constant C_PEDESTAL_SUPPRESSION_ADDR    : std_logic_vector(31 downto 0) := X"00001000";
constant C_SPI_FLASH_PROGRAMMER_ADDR    : std_logic_vector(31 downto 0) := X"00002000";
constant C_HV_CONTROL_ADDR              : std_logic_vector(31 downto 0) := X"00010000";
constant C_MONITOR_ADC_ADDR             : std_logic_vector(31 downto 0) := X"00010010";
constant C_READ_MADC_ADDR               : std_logic_vector(31 downto 0) := X"00010020";
constant C_USER_OUTPUT_ADDR             : std_logic_vector(31 downto 0) := X"00010030";
constant C_VERSION_ADDR                 : std_logic_vector(31 downto 0) := X"F0000000";


end RegisterAddress;

package body RegisterAddress is

end RegisterAddress;
