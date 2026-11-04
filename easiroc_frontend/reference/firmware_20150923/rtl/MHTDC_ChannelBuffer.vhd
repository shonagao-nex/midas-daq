--------------------------------------------------------------------------------
--! @file   MHTDC_ChannelBuffer.vhd
--! @brief  Channel buffer for MHTDC
--! @author Takehiro Shiozaki
--! @date   2014-06-07
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;

entity MHTDC_ChannelBuffer is
    port(
        TDC_CLK : in std_logic;
        RESET : in std_logic;
        DIN : in std_logic_vector(45 downto 0);
        WE : in std_logic;
        DOUT : out std_logic_vector(45 downto 0);
        RE : in std_logic;
        EMPTY : out std_logic;
        CLEAR : in std_logic
    );
end MHTDC_ChannelBuffer;

architecture RTL of MHTDC_ChannelBuffer is
    component DualPortRam is
        generic(
            G_WIDTH : integer;
            G_DEPTH : integer
        );
        port(
            WCLK : in  std_logic;
            DIN : in std_logic_vector(G_WIDTH - 1 downto 0);
            WADDR : in std_logic_vector(G_DEPTH - 1 downto 0);
            WE : in std_logic;

            RCLK : in std_logic;
            DOUT : out std_logic_vector(G_WIDTH - 1 downto 0);
            RADDR : in std_logic_vector(G_DEPTH - 1 downto 0)
        );
    end component;

    signal Ptr : std_logic_vector(3 downto 0);
    signal int_EMPTY : std_logic;
    signal MaskedRe : std_logic;
    signal NumberOfDataAvailable : std_logic_vector(4 downto 0);
    signal Raddr : std_logic_vector(3 downto 0);
begin
    DualPortRam_0: DualPortRam
    generic map(
        G_WIDTH => 46,
        G_DEPTH => 4
    )
    port map(
        WCLK => TDC_CLK,
        DIN => DIN,
        WADDR => Ptr,
        WE => WE,
        RCLK => TDC_CLK,
        DOUT => DOUT,
        RADDR => Raddr
    );
    Raddr <= Ptr - 1;

    process(TDC_CLK)
    begin
        if(TDC_CLK'event and TDC_CLK = '1') then
            if(RESET = '1') then
                Ptr <= (others => '0');
            else
                if(CLEAR = '1') then
                    Ptr <= (others => '0');
                elsif(WE = '1' and MaskedRe = '0') then
                    Ptr <= Ptr + 1;
                elsif(WE = '0' and MaskedRe = '1') then
                    Ptr <= Ptr - 1;
                end if;
            end if;
        end if;
    end process;

    process(TDC_CLK)
    begin
        if(TDC_CLK'event and TDC_CLK = '1') then
            if(RESET = '1') then
                NumberOfDataAvailable <= (others => '0');
            else
                if(CLEAR = '1') then
                    NumberOfDataAvailable <= (others => '0');
                elsif(WE = '1' and MaskedRe = '0' and NumberOfDataAvailable < 16) then
                    NumberOfDataAvailable <= NumberOfDataAvailable + 1;
                elsif(WE = '0' and MaskedRe = '1') then
                    NumberOfDataAvailable <= NumberOfDataAvailable - 1;
                end if;
            end if;
        end if;
    end process;

    MaskedRe <= RE and not int_EMPTY;
    int_EMPTY <= '1' when(NumberOfDataAvailable = 0) else
             '0';
    EMPTY <= int_EMPTY;

end RTL;
