--------------------------------------------------------------------------------
--! @file   SynchronizedDate.vhd
--! @brief  Auto generate Synchronized date
--! @author Takehiro Shiozaki
--! @date   2014-09-02
--------------------------------------------------------------------------------



library ieee;
use ieee.std_logic_1164.all;

package SynchronizedDate is
    constant C_SYNTHESIZED_DATE : std_logic_vector(31 downto 0) :=
        X"2015_02_20";
end SynchronizedDate;

package body SynchronizedDate is
end SynchronizedDate;

