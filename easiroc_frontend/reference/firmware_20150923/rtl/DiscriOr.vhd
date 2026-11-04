--------------------------------------------------------------------------------
--! @file   DiscriOr.vhd
--! @brief  or 64ch Discriminator signal
--! @author Takehiro Shiozaki
--! @date   2014-11-10
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;

entity DiscriOr is
    port(
        EASIROC1_TRIGGER : in std_logic_vector(31 downto 0);
        EASIROC2_TRIGGER : in std_logic_vector(31 downto 0);
        OR32U : out std_logic;
        OR32D : out std_logic;
        OR64 : out std_logic
    );
end DiscriOr;

architecture RTL of DiscriOr is
    function reduction_or(A: in std_logic_vector) return std_logic is
        variable ret : std_logic;
    begin
        ret := '0';
        for i in A'range loop
            ret := ret or A(i);
        end loop;
        return ret;
    end function;

    signal int_OR32U : std_logic;
    signal int_OR32D : std_logic;
begin
    int_OR32D <= reduction_or(EASIROC1_TRIGGER);
    int_OR32U <= reduction_or(EASIROC2_TRIGGER);

    OR32U <= int_OR32U;
    OR32D <= int_OR32D;
    OR64 <= int_OR32U or int_OR32D;
end RTL;
