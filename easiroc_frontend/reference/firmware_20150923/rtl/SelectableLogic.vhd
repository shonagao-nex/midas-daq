--------------------------------------------------------------------------------
--! @file   SelectableLogic.vhd
--! @brief  Manage selectable logic out
--! @author Naruhiro Chikuma
--! @date   2015-09-04
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;
use ieee.std_logic_arith.all;

entity SelectableLogic is
    generic(
        G_SELECTABLE_LOGIC_ADDRESS : std_logic_vector(31 downto 0) := X"00000000"
    );
    port(
        CLK         : in std_logic;
        RESET       : in std_logic;
        TRIGGER_IN1 : in std_logic_vector(31 downto 0);
        TRIGGER_IN2 : in std_logic_vector(31 downto 0);
        RBCP_ACT    : in std_logic;
        RBCP_ADDR   : in std_logic_vector(31 downto 0);
        RBCP_WE     : in std_logic;
        RBCP_WD     : in std_logic_vector(7 downto 0);
        RBCP_ACK    : out std_logic;
        SELECTABLE_LOGIC : out std_logic
    );
end SelectableLogic;

architecture RTL of SelectableLogic is

    function reduction_or(A: in std_logic_vector) return std_logic is
        variable ret : std_logic;
    begin
        ret := '0';
        for i in A'range loop
            ret := ret or A(i);
        end loop;
        return ret;
    end function;

    function selected_and(TRIG_IN,SEL_CHANNEL: in std_logic_vector) return std_logic is
	    variable ret : std_logic;
    begin
	    ret := '1';
	    if(SEL_CHANNEL = 0) then
		    ret := '0';
	    else
		    for i in TRIG_IN'range loop
			    if(SEL_CHANNEL(i) = '1') then
				    ret := ret and TRIG_IN(i);
			    end if;
		    end loop;
	    end if;
	    return ret;
    end function;
 
    function hit_num(TRIG_IN, NUMth: in std_logic_vector) return std_logic is
        variable num : std_logic_vector(6 downto 0);
        variable ret : std_logic;
    begin
        num := (others => '0');
	ret := '0';
        for i in TRIG_IN'range loop
		if(TRIG_IN(i) = '1') then
			num := num + 1;
		end if;
        end loop;
	
	if(num>=NUMth) then
		ret := '1';
	end if;

        return ret;
    end function;

    component RBCP_Receiver
		generic (
			G_ADDR : std_logic_vector(31 downto 0);
			G_LEN : integer;
			G_ADDR_WIDTH : integer
		);
		port(
            		CLK : in std_logic;
            		RESET : in std_logic;
            		RBCP_ACT : in std_logic;
            		RBCP_ADDR : in std_logic_vector(31 downto 0);
            		RBCP_WE : in std_logic;
            		RBCP_WD : in std_logic_vector(7 downto 0);
            		RBCP_ACK : out std_logic;
            		ADDR : out std_logic_vector(G_ADDR_WIDTH -1 downto 0);
            		WE : out std_logic;
            		WD : out std_logic_vector(7 downto 0)
		);
    end component;

    signal addr_recv : std_logic_vector(3 downto 0);
    signal wd_recv   : std_logic_vector(7 downto 0);
    signal we_recv   : std_logic;

    signal Pattern          : std_logic_vector(7  downto 0);
    signal Channel          : std_logic_vector(7  downto 0);
    signal HitNumThreshold  : std_logic_vector(7  downto 0);
    signal AndLogicChannel1 : std_logic_vector(31 downto 0);
    signal AndLogicChannel2 : std_logic_vector(31 downto 0);
    signal AndLogicChannel  : std_logic_vector(63 downto 0);

    signal EasirocTrigger  : std_logic_vector(63 downto 0);

    signal Active16uu : std_logic;  
    signal Active16ud : std_logic;
    signal Active16du : std_logic;
    signal Active16dd : std_logic;
    signal Active16   : std_logic;
    signal Active32u  : std_logic;
    signal Active32d  : std_logic;
    signal Active32   : std_logic;
    signal Active64   : std_logic;

    signal Or16uu  : std_logic;
    signal Or16ud  : std_logic;
    signal Or16du  : std_logic;
    signal Or16dd  : std_logic;
    signal Or32u   : std_logic;
    signal Or32d   : std_logic;
    signal Or64    : std_logic;
    signal Or16And : std_logic;
    signal Or32And : std_logic;
    signal And32u  : std_logic;
    signal And32d  : std_logic;
    signal And64   : std_logic;
    signal And32Or : std_logic;


begin

    RBCP_Receiver_0: RBCP_Receiver
    generic map(
        G_ADDR       => G_SELECTABLE_LOGIC_ADDRESS,
        G_LEN        => 11,
        G_ADDR_WIDTH => 4
    )
    port map(
        CLK       => CLK,
        RESET     => RESET,
        RBCP_ACT  => RBCP_ACT,
        RBCP_ADDR => RBCP_ADDR,
        RBCP_WE   => RBCP_WE,
        RBCP_WD   => RBCP_WD,
        RBCP_ACK  => RBCP_ACK,
        ADDR      => addr_recv,
        WE        => we_recv,
        WD        => wd_recv
    );	


    process(CLK)
    begin
        if(CLK'event and CLK = '1') then
            if(RESET = '1') then
		Pattern <= (others => '0');
		hitNumThreshold <= (others => '0');
		AndLogicChannel1 <= (others => '0');
		AndLogicChannel2 <= (others => '0');
            else
                if(we_recv = '1') then
			if( addr_recv = 0 ) then
			        Pattern <= wd_recv(7 downto 0);
			elsif( addr_recv = 1 ) then
			        Channel <= wd_recv(7 downto 0);
			elsif( addr_recv = 2 ) then
			        HitNumThreshold(7 downto 0) <= wd_recv(7 downto 0);
			elsif( addr_recv = 3 ) then
			        AndLogicChannel2(31 downto 24) <= wd_recv(7 downto 0);
			elsif( addr_recv = 4 ) then
			        AndLogicChannel2(23 downto 16) <= wd_recv(7 downto 0);
			elsif( addr_recv = 5 ) then
			        AndLogicChannel2(15 downto 8) <= wd_recv(7 downto 0);
			elsif( addr_recv = 6 ) then
			        AndLogicChannel2(7 downto 0) <= wd_recv(7 downto 0);
			elsif( addr_recv = 7 ) then
			        AndLogicChannel1(31 downto 24) <= wd_recv(7 downto 0);
			elsif( addr_recv = 8 ) then
			        AndLogicChannel1(23 downto 16) <= wd_recv(7 downto 0);
			elsif( addr_recv = 9 ) then
			        AndLogicChannel1(15 downto 8) <= wd_recv(7 downto 0);
			elsif( addr_recv = 10 ) then
			        AndLogicChannel1(7 downto 0) <= wd_recv(7 downto 0);
			else
			        Pattern <= (others => '0');
			        hitNumThreshold  <= (others => '0');
			        AndLogicChannel1 <= (others => '0');
			        AndLogicChannel2 <= (others => '0');
			end if;
                end if;
            end if;
        end if;
    end process;

    EasirocTrigger  <= TRIGGER_IN2 & TRIGGER_IN1;

    Active16uu <= hit_num(TRIGGER_IN1(15 downto 0) ,HitNumThreshold);
    Active16ud <= hit_num(TRIGGER_IN1(31 downto 16),HitNumThreshold);
    Active16du <= hit_num(TRIGGER_IN2(15 downto 0) ,HitNumThreshold);
    Active16dd <= hit_num(TRIGGER_IN2(31 downto 16),HitNumThreshold);
    Active16   <= Active16uu and Active16ud and Active16du and Active16dd;
    Active32u  <= hit_num(TRIGGER_IN1(31 downto 0) ,HitNumThreshold);
    Active32d  <= hit_num(TRIGGER_IN2(31 downto 0) ,HitNumThreshold);
    Active32   <= Active32u and Active32d;
    Active64   <= hit_num(EasirocTrigger(63 downto 0)  ,HitNumThreshold);

    Or16uu <= reduction_or(TRIGGER_IN1(15 downto 0));
    Or16ud <= reduction_or(TRIGGER_IN1(31 downto 16));
    Or16du <= reduction_or(TRIGGER_IN2(15 downto 0));
    Or16dd <= reduction_or(TRIGGER_IN2(31 downto 16));
    Or32u  <= (Or16uu or Or16ud);
    Or32d  <= (Or16du or Or16dd);
    Or64   <= (Or32u  or Or32d );

    Or16And <= Or16uu and Or16ud and Or16du and Or16dd;
    Or32And <= Or32u and Or32d;

    AndLogicChannel <= AndLogicChannel2 & AndLogicChannel1;
    And32u  <= selected_and(TRIGGER_IN1,AndLogicChannel1);
    And32d  <= selected_and(TRIGGER_IN2,AndLogicChannel2);
    And64   <= selected_and(EasirocTrigger,AndLogicChannel);
    And32Or <= And32u or And32d;

    process(Channel,Pattern,EasirocTrigger,
            Or32u,Or32d,Or64,Or32And,Or16And,And32u,And32d,And64,And32Or,
            Active32u,Active32d,Active64,Active32,Active16)
    begin
	    if(Pattern = 0) then
		    SELECTABLE_LOGIC <= EasirocTrigger(conv_integer(Channel));
	    elsif(Pattern = 1) then
		    SELECTABLE_LOGIC <= Or32u   and Active32u;
	    elsif(Pattern = 2) then
		    SELECTABLE_LOGIC <= Or32d   and Active32d;
	    elsif(Pattern = 3) then
		    SELECTABLE_LOGIC <= Or64    and Active64; 
	    elsif(Pattern = 4) then
		    SELECTABLE_LOGIC <= Or32And and Active32; 
	    elsif(Pattern = 5) then
		    SELECTABLE_LOGIC <= Or16And and Active16; 
	    elsif(Pattern = 6) then
		    SELECTABLE_LOGIC <= And32u;
	    elsif(Pattern = 7) then
		    SELECTABLE_LOGIC <= And32d;
	    elsif(Pattern = 8) then
		    SELECTABLE_LOGIC <= And64;
	    elsif(Pattern = 9) then
		    SELECTABLE_LOGIC <= And32Or;
	    else
		    SELECTABLE_LOGIC <= '0';
	    end if;
   end process;


end RTL;
