--------------------------------------------------------------------------------
--! @file   MHTDC_DataType.vhd
--! @brief  type declare for MHTDC
--! @author Takehiro Shiozaki
--! @date   2014-06-10
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;

package MHTDC_DataType is
    type MHTDC_CounterArray is array (63 downto 0)
        of std_logic_vector(45 downto 0);
end MHTDC_DataType;
