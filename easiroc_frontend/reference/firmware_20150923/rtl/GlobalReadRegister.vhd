--------------------------------------------------------------------------------
--! @file   GlobalReadRegister.vhd
--! @brief  Integrate 2 ReadRegister
--! @author Takehiro Shiozaki
--! @date   2013-11-14
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;

entity GlobalReadRegister is
	generic (G_READ_REGISTER1_ADDR : std_logic_vector(31 downto 0);
	         G_READ_REGISTER2_ADDR : std_logic_vector(31 downto 0)
				);
    port ( CLK : in  std_logic;
           RESET : in  std_logic;
			  READ_REGISTER_CLK : in std_logic;

			  -- RBCP Interface
			  RBCP_ACT : in std_logic;
			  RBCP_ADDR : in std_logic_vector(31 downto 0);
			  RBCP_WE : in std_logic;
			  RBCP_WD : in std_logic_vector(7 downto 0);
			  RBCP_ACK : out std_logic;

			  -- EASIROC1 ReadRegister Interface
			  SRIN_READ1 : out std_logic;
			  CLK_READ1 : out std_logic;

			  -- EASIROC2 ReadRegister Interface
			  SRIN_READ2 : out std_logic;
			  CLK_READ2 : out std_logic
			  );
end GlobalReadRegister;

architecture RTL of GlobalReadRegister is

	component ReadRegister
	generic(
		G_READ_REGISTER_ADDR : std_logic_vector(31 downto 0)
		);
	port(
		CLK : in std_logic;
		RESET : in std_logic;
		READ_REGISTER_CLK : in std_logic;
		RBCP_ACT : in std_logic;
		RBCP_ADDR : in std_logic_vector(31 downto 0);
		RBCP_WE : in std_logic;
		RBCP_WD : in std_logic_vector(7 downto 0);
		RBCP_ACK : out std_logic;
		SRIN_READ : out std_logic;
		CLK_READ : out std_logic
		);
	end component;

	signal RbcpAckReadRegister1 : std_logic;
	signal RbcpAckReadRegister2 : std_logic;

begin

	ReadRegister_1: ReadRegister
	generic map(
		G_READ_REGISTER_ADDR => G_READ_REGISTER1_ADDR
	)
	port map(
		CLK => CLK,
		RESET => RESET,
		READ_REGISTER_CLK => READ_REGISTER_CLK,
		RBCP_ACT => RBCP_ACT,
		RBCP_ADDR => RBCP_ADDR,
		RBCP_WE => RBCP_WE,
		RBCP_WD => RBCP_WD,
		RBCP_ACK => RbcpAckReadRegister1,
		SRIN_READ => SRIN_READ1,
		CLK_READ => CLK_READ1
	);

	ReadRegister_2: ReadRegister
	generic map(
		G_READ_REGISTER_ADDR => G_READ_REGISTER2_ADDR
	)
	port map(
		CLK => CLK,
		RESET => RESET,
		READ_REGISTER_CLK => READ_REGISTER_CLK,
		RBCP_ACT => RBCP_ACT,
		RBCP_ADDR => RBCP_ADDR,
		RBCP_WE => RBCP_WE,
		RBCP_WD => RBCP_WD,
		RBCP_ACK => RbcpAckReadRegister2,
		SRIN_READ => SRIN_READ2,
		CLK_READ => CLK_READ2
	);

	RBCP_ACK <= RbcpAckReadRegister1 or RbcpAckReadRegister2;

end RTL;

