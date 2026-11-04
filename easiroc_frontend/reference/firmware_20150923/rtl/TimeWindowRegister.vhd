--------------------------------------------------------------------------------
--! @file   TimeWindowRegister.vhd
--! @brief  TimeWindow register
--! @author Takehiro Shiozaki
--! @date   2015-02-17
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_arith.all;

entity TimeWindowRegister is
    generic (
        G_TIME_WINDOW_REGISTER_ADDRESS : std_logic_vector(31 downto 0) := X"00000000"
    );
    port (
        CLK : in  std_logic;
        RESET : in  std_logic;

        -- RBCP Interface
        RBCP_ACT : in std_logic;
        RBCP_ADDR : in std_logic_vector(31 downto 0);
        RBCP_WE : in std_logic;
        RBCP_WD : in std_logic_vector(7 downto 0);
        RBCP_ACK : out std_logic;

        TIME_WINDOW : out std_logic_vector(11 downto 0)
    );
end TimeWindowRegister;

architecture RTL of TimeWindowRegister is
    constant C_TIME_WINDOW_REGISTER_ADDRESS : std_logic_vector(31 downto 0) := G_TIME_WINDOW_REGISTER_ADDRESS;

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

    RBCP_Receiver_0 : RBCP_Receiver
    generic map(
        G_ADDR => C_TIME_WINDOW_REGISTER_ADDRESS,
        G_LEN => 2,
        G_ADDR_WIDTH => 1
    )
    port map(
        CLK => CLK,
        RESET => RESET,
        RBCP_ACT => RBCP_ACT,
        RBCP_ADDR => RBCP_ADDR,
        RBCP_WE => RBCP_WE,
        RBCP_WD => RBCP_WD,
        RBCP_ACK => RBCP_ACK,
        ADDR => Waddr,
        WE => We,
        Wd => Wd
    );

    process(CLK)
    begin
        if(CLK'event and CLK = '1') then
            if(We = '1') then
                if(Waddr(0) = '0') then
                    TIME_WINDOW(11 downto 8) <= Wd(3 downto 0);
                else
                    TIME_WINDOW( 7 downto 0) <= Wd;
                end if;
            end if;
        end if;
    end process;

end RTL;
