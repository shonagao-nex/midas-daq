
--------------------------------------------------------------------------------
--! @file   RBCP_Distributor.vhd
--! @brief  Gather RBCP signals(RBCP_ACK and RBCP_RD)
--! @author Naruhiro Chikuma
--! @date   2015-09-06
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;

entity RBCP_Distributor is
    port(
        ACK_IN1 : in std_logic;
        ACK_IN2 : in std_logic;
        ACK_IN3 : in std_logic;
        ACK_IN4 : in std_logic;
        ACK_IN5 : in std_logic;
        ACK_IN6 : in std_logic;
        ACK_IN7 : in std_logic;
        ACK_IN8 : in std_logic;
        ACK_IN9 : in std_logic;
        ACK_INA : in std_logic;
        ACK_INB : in std_logic;
        ACK_INC : in std_logic;
        ACK_IND : in std_logic;
        RD_IN1 : in std_logic_vector(7 downto 0);
        RD_IN2 : in std_logic_vector(7 downto 0);
        RD_INA : in std_logic_vector(7 downto 0);
        RD_OUT : out std_logic_vector(7 downto 0);
        ACK_OUT : out std_logic
    );
end RBCP_Distributor;

architecture RTL of RBCP_Distributor is

    signal ack : std_logic_vector(4 downto 0);

begin

    ACK_OUT <= ACK_IN1 or ACK_IN2 or ACK_IN3 or ACK_IN4 or
               ACK_IN5 or ACK_IN6 or ACK_IN7 or ACK_IN8 or ACK_IN9 or 
	       ACK_INA or ACK_INB or ACK_INC or ACK_IND;

    RD_OUT <= RD_IN1 when(ACK_IN1 = '1') else
              RD_IN2 when(ACK_IN2 = '1') else
              RD_INA when(ACK_INA = '1') else
              (others => '0');
end RTL;

