--------------------------------------------------------------------------------
--! @file   StatusRegister.vhd
--! @brief  Status register
--! @author Takehiro Shiozaki
--! @date   2013-11-14
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use work.Asynch.all;

entity StatusRegister is
    generic (
        G_STATUS_REGISTER_ADDR : std_logic_vector(31 downto 0) := X"00000000");
    port (
        CLK : in  std_logic;
        RESET : in  std_logic;

        -- RBCP Interface
        RBCP_ACT : in std_logic;
        RBCP_ADDR : in std_logic_vector(31 downto 0);
        RBCP_WE : in std_logic;
        RBCP_WD : in std_logic_vector(7 downto 0);
        RBCP_ACK : out std_logic;

        DAQ_MODE : out std_logic;
        SEND_ADC : out std_logic;
        SEND_TDC : out std_logic;
        SEND_SCALER : out std_logic
    );
end StatusRegister;

architecture RTL of StatusRegister is

    constant C_STATUS_REGISTER_ADDR : std_logic_vector(31 downto 0) := G_STATUS_REGISTER_ADDR;

    component RBCP_Receiver
    generic (
        G_ADDR : std_logic_vector(31 downto 0);
        G_LEN : integer;
        G_ADDR_WIDTH : integer
    );
    port(
        CLK : in  std_logic;
        RESET : in  std_logic;
        RBCP_ACT : in  std_logic;
        RBCP_ADDR : in  std_logic_vector(31 downto 0);
        RBCP_WE : in  std_logic;
        RBCP_WD : in  std_logic_vector(7 downto 0);
        RBCP_ACK : out  std_logic;
        ADDR : out  std_logic_vector(G_ADDR_WIDTH - 1 downto 0);
        WE : out  std_logic;
        WD : out  std_logic_vector(7 downto 0)
    );
    end component;

    signal We : std_logic;
    signal Waddr : std_logic_vector(0 downto 0);
    signal Wd : std_logic_vector(7 downto 0);

begin

    RBCP_Receiver_0: RBCP_Receiver
    generic map(
        G_ADDR => C_STATUS_REGISTER_ADDR,
        G_LEN => 1,
        G_ADDR_WIDTH => 1
    )
    port map (
        CLK => CLK,
        RESET => RESET,
        RBCP_ACT => RBCP_ACT,
        RBCP_ADDR => RBCP_ADDR,
        RBCP_WE => RBCP_WE,
        RBCP_WD => RBCP_WD,
        RBCP_ACK => RBCP_ACK,
        ADDR => Waddr,
        WE => We,
        WD => Wd
    );

    process(CLK, RESET)
    begin
        if(RESET = '1') then
            DAQ_MODE <= '0';
            SEND_ADC <= '0';
            SEND_TDC <= '0';
        elsif(CLK'event and CLK = '1') then
            if(We = '1' and Waddr(0) = '0') then
            DAQ_MODE <= Wd(0);
            SEND_ADC <= Wd(1);
            SEND_TDC <= Wd(2);
            SEND_SCALER <= Wd(3);
            end if;
        end if;
    end process;

end RTL;

