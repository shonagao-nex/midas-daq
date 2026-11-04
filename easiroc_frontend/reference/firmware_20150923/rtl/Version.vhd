--------------------------------------------------------------------------------
--! @file   Version.vhd
--! @brief  Version and Synthesized date
--! @author Takehiro Shiozaki
--! @date   2013-11-05
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;

entity Version is
    generic (
        G_VERSION_ADDR : std_logic_vector(31 downto 0);
        G_VERSION : std_logic_vector(15 downto 0);
        G_SYNTHESIZED_DATE : std_logic_vector(31 downto 0)
    );
    port (
        CLK : in  std_logic;
        RESET : in  std_logic;

        -- RBCP interface
        RBCP_ACT : in std_logic;
        RBCP_ADDR : in std_logic_vector(31 downto 0);
        RBCP_RE : in std_logic;
        RBCP_RD : out std_logic_vector(7 downto 0);
        RBCP_ACK : out std_logic
    );
end Version;

architecture RTL of Version is

    constant C_VERSION_ADDR : std_logic_vector(31 downto 0) := G_VERSION_ADDR;
    constant C_VERSION : std_logic_vector(15 downto 0) := G_VERSION;
    constant C_SYNTHESIZED_DATE : std_logic_vector(31 downto 0) := G_SYNTHESIZED_DATE;

    component RBCP_Sender
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
            RBCP_RE : in  std_logic;
            RBCP_RD : out  std_logic_vector(7 downto 0);
            RBCP_ACK : out  std_logic;
            ADDR : out  std_logic_vector(G_ADDR_WIDTH - 1 downto 0);
            RD : in  std_logic_vector(7 downto 0)
        );
    end component;

    signal Addr : std_logic_vector(2 downto 0);
    signal Rd : std_logic_vector(7 downto 0);

begin

    RBCP_Sender_0: RBCP_Sender
    generic map(
        G_ADDR => C_VERSION_ADDR,
        G_LEN => 6,
        G_ADDR_WIDTH => 3
    )
    port map (
        CLK => CLK,
        RESET => RESET,
        RBCP_ACT => RBCP_ACT,
        RBCP_ADDR => RBCP_ADDR,
        RBCP_RE => RBCP_RE,
        RBCP_RD => RBCP_RD,
        RBCP_ACK => RBCP_ACK,
        ADDR => Addr,
        RD => Rd
    );

    process(CLK, RESET)
    begin
        if(RESET = '1') then
            Rd <= (others => '0');
        elsif(CLK'event and CLK = '1') then
            case Addr is
                when "000" =>
                    Rd <= C_VERSION(15 downto 8);
                when "001" =>
                    Rd <= C_VERSION(7 downto 0);
                when "010" =>
                    Rd <= C_SYNTHESIZED_DATE(31 downto 24);
                when "011" =>
                    Rd <= C_SYNTHESIZED_DATE(23 downto 16);
                when "100" =>
                    Rd <= C_SYNTHESIZED_DATE(15 downto 8);
                when "101" =>
                    Rd <= C_SYNTHESIZED_DATE(7 downto 0);
                when others =>
                    Rd <= (others => '1');
            end case;
        end if;
    end process;

end RTL;

