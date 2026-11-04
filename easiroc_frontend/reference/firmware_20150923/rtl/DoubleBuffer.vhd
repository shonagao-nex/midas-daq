--------------------------------------------------------------------------------
--! @file   DoubleBuffer.vhd
--! @brief  Double buffer
--! @author Takehiro Shiozaki
--! @date   2014-11-04
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;

entity DoubleBuffer is
    generic (
        G_WIDTH : integer;
        G_DEPTH : integer
    );
    port (
        RESET : in  std_logic;

        -- write interface
        WCLK : in  std_logic;
        DIN : in std_logic_vector(G_WIDTH - 1 downto 0);
        WADDR : in std_logic_vector(G_DEPTH - 1 downto 0);
        WE : in std_logic;
        WCOMP : in std_logic;
        DEC_WPTR : in std_logic;
        FULL : out std_logic;

        -- read interface
        RCLK : in std_logic;
        DOUT : out std_logic_vector(G_WIDTH - 1 downto 0);
        RADDR : in std_logic_vector(G_DEPTH - 1 downto 0);
        RCOMP : in std_logic;
        EMPTY : out std_logic
    );
end DoubleBuffer;

architecture RTL of DoubleBuffer is

    component DualPortRam
    generic(
        G_WIDTH : integer;
        G_DEPTH : integer
    );
    port(
        WCLK : in  std_logic;
        DIN : in  std_logic_vector(G_WIDTH - 1 downto 0);
        WADDR : in  std_logic_vector(G_DEPTH - 1 downto 0);
        WE : in  std_logic;
        RCLK : in  std_logic;
        DOUT : out  std_logic_vector(G_WIDTH - 1 downto 0);
        RADDR : in  std_logic_vector(G_DEPTH - 1 downto 0)
    );
    end component;

    component SynchronizerNbit
    generic (
        G_BITS : integer
    );
    port(
        CLK : in  std_logic;
        RESET : in  std_logic;
        DIN : in  std_logic_vector(G_BITS - 1 downto 0);
        DOUT : out  std_logic_vector(G_BITS - 1 downto 0)
    );
    end component;

    signal Din0 : std_logic_vector(G_WIDTH - 1 downto 0);
    signal Waddr0 : std_logic_vector(G_DEPTH - 1 downto 0);
    signal We0 : std_logic;
    signal Dout0 : std_logic_vector(G_WIDTH - 1 downto 0);
    signal Raddr0 : std_logic_vector(G_DEPTH - 1 downto 0);

    signal Din1 : std_logic_vector(G_WIDTH - 1 downto 0);
    signal Waddr1 : std_logic_vector(G_DEPTH - 1 downto 0);
    signal We1 : std_logic;
    signal Dout1 : std_logic_vector(G_WIDTH - 1 downto 0);
    signal Raddr1 : std_logic_vector(G_DEPTH - 1 downto 0);

    signal Wptr : std_logic_vector(1 downto 0);
    signal GraycodedWptr : std_logic_vector(1 downto 0);
    signal SynchWptr : std_logic_vector(1 downto 0);

    signal Rptr : std_logic_vector(1 downto 0);
    signal GraycodedRptr : std_logic_vector(1 downto 0);
    signal SynchRptr : std_logic_vector(1 downto 0);

begin

    DualPortRam_0: DualPortRam
    generic map (
        G_WIDTH => G_WIDTH,
        G_DEPTH => G_DEPTH
    )
    port map (
        WCLK => WCLK,
        DIN => Din0,
        WADDR => Waddr0,
        WE => We0,
        RCLK => RCLK,
        DOUT => Dout0,
        RADDR => Raddr0
    );

    DualPortRam_1: DualPortRam
    generic map (
        G_WIDTH => G_WIDTH,
        G_DEPTH => G_DEPTH
    )
    port map (
        WCLK => WCLK,
        DIN => Din1,
        WADDR => Waddr1,
        WE => We1,
        RCLK => RCLK,
        DOUT => Dout1,
        RADDR => Raddr1
    );

    process(WCLK, RESET)
    begin
        if(RESET = '1') then
            Wptr <= (others => '0');
        elsif(WCLK'event and WCLK = '1') then
            if(WCOMP = '1' and DEC_WPTR = '0') then
                Wptr <= Wptr + 1;
            elsif(WCOMP = '0' and DEC_WPTR = '1') then
                Wptr <= Wptr - 1;
            end if;
        end if;
    end process;

    process(RCLK, RESET)
    begin
        if(RESET = '1') then
            Rptr <= (others => '0');
        elsif(RCLK'event and RCLK = '1') then
            if(RCOMP = '1') then
                Rptr <= Rptr + 1;
            end if;
        end if;
    end process;

    process(Wptr, DIN, WADDR, WE)
    begin
        if(Wptr(0) = '0') then
            Din0 <= DIN;
            Waddr0 <= WADDR;
            We0 <= WE;
            Din1 <= (others => '0');
            Waddr1 <= (others => '0');
            We1 <= '0';
        else
            Din0 <= (others => '0');
            Waddr0 <= (others => '0');
            We0 <= '0';
            Din1 <= DIN;
            Waddr1 <= WADDR;
            We1 <= WE;
        end if;
    end process;

    process(Rptr, RADDR, Dout0, Dout1)
    begin
        if(Rptr(0) = '0') then
            DOUT <= Dout0;
            Raddr0 <= RADDR;
            Raddr1 <= (others => '0');
        else
            DOUT <= Dout1;
            Raddr0 <= (others => '0');
            Raddr1 <= RADDR;
        end if;
    end process;

    GraycodedWptr <= Wptr xor ('0' & Wptr(1));
    GraycodedRptr <= Rptr xor ('0' & Rptr(1));

    Synchronizer_Wptr: SynchronizerNbit
    generic map(
        G_BITS => 2
    )
    port map (
        CLK => RCLK,
        RESET => RESET,
        DIN => GraycodedWptr,
        DOUT => SynchWptr
    );

    Synchronizer_Rptr: SynchronizerNbit
    generic map(
        G_BITS => 2
    )
    port map (
        CLK => WCLK,
        RESET => RESET,
        DIN => GraycodedRptr,
        DOUT => SynchRptr
    );

    FULL <= '1' when(GraycodedWptr = not SynchRptr) else
            '0';
    EMPTY <= '1' when(SynchWptr = GraycodedRptr) else
             '0';

end RTL;

