--------------------------------------------------------------------------------
--! @file   GlobalSlowControl.vhd
--! @brief  Integrate 2 SlowControl
--! @author Takehiro Shiozaki
--! @date   2013-11-14
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;

entity GlobalSlowControl is
	 generic(
			G_SLOW_CONTROL1_ADDR : std_logic_vector(31 downto 0);
			G_SLOW_CONTROL2_ADDR : std_logic_vector(31 downto 0)
			);
    port ( CLK : in  std_logic;
           RESET : in  std_logic;
			  SLOW_CONTROL_CLK : in std_logic;

			  -- RBCP Interface
			  RBCP_ACT : in std_logic;
			  RBCP_ADDR : in std_logic_vector(31 downto 0);
			  RBCP_WE : in std_logic;
			  RBCP_WD : in std_logic_vector(7 downto 0);
			  RBCP_ACK : out std_logic;

			  -- EASIROC1 Contol Interface
			  START_CYCLE1 : in std_logic;
			  SELECT_SC1 : in std_logic;

			  -- EASIROC1 SlowControl Interface
			  SRIN_SR1 : out std_logic;
			  CLK_SR1 : out std_logic;

			  -- EASIROC2 Contol Interface
			  START_CYCLE2 : in std_logic;
			  SELECT_SC2 : in std_logic;

			  -- EASIROC2 SlowControl Interface
			  SRIN_SR2 : out std_logic;
			  CLK_SR2 : out std_logic
			  );
end GlobalSlowControl;

architecture RTL of GlobalSlowControl is

	component SlowControl
	generic(
		G_SLOW_CONTROL_ADDR : std_logic_vector(31 downto 0)
		);
	port(
		CLK : in std_logic;
		RESET : in std_logic;
		SLOW_CONTROL_CLK : in std_logic;
		RBCP_ACT : in std_logic;
		RBCP_ADDR : in std_logic_vector(31 downto 0);
		RBCP_WE : in std_logic;
		RBCP_WD : in std_logic_vector(7 downto 0);
		START_CYCLE : in std_logic;
		SELECT_SC : in std_logic;
		RBCP_ACK : out std_logic;
		SRIN_SR : out std_logic;
		CLK_SR : out std_logic
		);
	end component;

	signal RbcpAckSlowControl1 : std_logic;
	signal RbcpAckSlowControl2 : std_logic;

begin

	SlowControl_1: SlowControl
	generic map(
		G_SLOW_CONTROL_ADDR => G_SLOW_CONTROL1_ADDR
		)
	port map(
		CLK => CLK,
		RESET => RESET,
		SLOW_CONTROL_CLK => SLOW_CONTROL_CLK,
		RBCP_ACT => RBCP_ACT,
		RBCP_ADDR => RBCP_ADDR,
		RBCP_WE => RBCP_WE,
		RBCP_WD => RBCP_WD,
		RBCP_ACK => RbcpAckSlowControl1,
		START_CYCLE => START_CYCLE1,
		SELECT_SC => SELECT_SC1,
		SRIN_SR => SRIN_SR1,
		CLK_SR => CLK_SR1
	);

	SlowControl_2: SlowControl
	generic map(
		G_SLOW_CONTROL_ADDR => G_SLOW_CONTROL2_ADDR
		)
	port map(
		CLK => CLK,
		RESET => RESET,
		SLOW_CONTROL_CLK => SLOW_CONTROL_CLK,
		RBCP_ACT => RBCP_ACT,
		RBCP_ADDR => RBCP_ADDR,
		RBCP_WE => RBCP_WE,
		RBCP_WD => RBCP_WD,
		RBCP_ACK => RbcpAckSlowControl2,
		START_CYCLE => START_CYCLE2,
		SELECT_SC => SELECT_SC2,
		SRIN_SR => SRIN_SR2,
		CLK_SR => CLK_SR2
	);

	RBCP_ACK <= RbcpAckSlowControl1 or RbcpAckSlowControl2;
end RTL;

