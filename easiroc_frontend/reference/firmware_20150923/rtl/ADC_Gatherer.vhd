--------------------------------------------------------------------------------
--! @file   ADC_Gatherer.vhd
--! @brief  Gather 4 X 64ch ADC data to FIFO
--! @author Takehiro Shiozaki
--! @date   2014-05-05
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;

entity ADC_Gatherer is
    port ( CLK : in  std_logic;
           RESET : in  std_logic;

			  -- ADC0
           DIN0 : in  std_logic_vector (20 downto 0);
           RADDR0 : out  std_logic_vector (5 downto 0);
           RCOMP0 : out  std_logic;
           EMPTY0 : in  std_logic;

			  -- ADC1
			  DIN1 : in  std_logic_vector (20 downto 0);
           RADDR1 : out  std_logic_vector (5 downto 0);
           RCOMP1 : out  std_logic;
           EMPTY1 : in  std_logic;

			  -- ADC2
			  DIN2 : in  std_logic_vector (20 downto 0);
           RADDR2 : out  std_logic_vector (5 downto 0);
           RCOMP2 : out  std_logic;
           EMPTY2 : in  std_logic;

			  -- ADC3
			  DIN3 : in  std_logic_vector (20 downto 0);
           RADDR3 : out  std_logic_vector (5 downto 0);
           RCOMP3 : out  std_logic;
           EMPTY3 : in  std_logic;

			  -- FIFO
			  DOUT : out std_logic_vector(20 downto 0);
			  WE : out std_logic;
			  FULL : in std_logic;

			  -- Control
			  START : in std_logic;
			  BUSY : out std_logic
			  );
end ADC_Gatherer;

architecture RTL of ADC_Gatherer is
	signal Din : std_logic_vector(20 downto 0);
	signal IsFooter : std_logic;
	signal DoutEnable : std_logic;
	signal Ready : std_logic;
	signal Rcomp : std_logic;

	signal Raddr : std_logic_vector(5 downto 0);
	signal RaddrCountUp : std_logic;
	signal RaddrCountClear : std_logic;

	signal DinSel : std_logic_vector(1 downto 0);
	signal DinSelCountUp : std_logic;
	signal DinSelCountClear : std_logic;

	type State is (IDLE, WAIT_READY, READ_DATA, STORE_DATA, WAIT_FULL,
	               WRITE_DATA, INC_DIN_SEL, CLEAR_RADDR, READ_COMPLETE);
	signal CurrentState : State;
	signal NextState : State;
begin

	process(DIN0, DIN1, DIN2, DIN3, DinSel)
	begin
		case DinSel is
			when "00" =>
				Din <= DIN0;
			when "01" =>
				Din <= DIN1;
			when "10" =>
				Din <= DIN2;
			when others =>
				Din <= DIN3;
		end case;
	end process;

	IsFooter <= Din(20);

	process(CLK, RESET)
	begin
		if(RESET = '1') then
			DOUT <= (others => '0');
		elsif(CLK'event and CLK = '1') then
			if(DoutEnable = '1') then
				DOUT <= Din;
			end if;
		end if;
	end process;

	Ready <= not(EMPTY0 or EMPTY1 or EMPTY2 or EMPTY3);
	RCOMP0 <= Rcomp;
	RCOMP1 <= Rcomp;
	RCOMP2 <= Rcomp;
	RCOMP3 <= Rcomp;

	process(CLK, RESET)
	begin
		if(RESET = '1') then
			Raddr <= (others => '0');
		elsif(CLk'event and CLK = '1') then
			if(RaddrCountClear = '1') then
				Raddr <= (others => '0');
			elsif(RaddrCountUp = '1') then
				Raddr <= Raddr + 1;
			end if;
		end if;
	end process;

	RADDR0 <= Raddr;
	RADDR1 <= Raddr;
	RADDR2 <= Raddr;
	RADDR3 <= Raddr;

	process(CLK, RESET)
	begin
		if(RESET = '1') then
			DinSel <= (others => '0');
		elsif(CLk'event and CLK = '1') then
			if(DinSelCountClear = '1') then
				DinSel <= (others => '0');
			elsif(DinSelCountUp = '1') then
				DinSel <= DinSel + 1;
			end if;
		end if;
	end process;

	process(CLK, RESET)
	begin
		if(RESET = '1') then
			CurrentState <= IDLE;
		elsif(CLK'event and CLK = '1') then
			CurrentState <= NextState;
		end if;
	end process;

	process(CurrentState, START, IsFooter, DinSel, FULL, Ready)
	begin
		case CurrentState is
			when IDLE =>
				if(START = '0') then
					NextState <= CurrentState;
				else
					if(Ready = '1') then
						NextState <= READ_DATA;
					else
						NextState <= WAIT_READY;
					end if;
				end if;
			when WAIT_READY =>
				if(Ready = '1') then
					NextState <= READ_DATA;
				else
					NextState <= CurrentState;
				end if;
			when READ_DATA =>
				NextState <= STORE_DATA;
			when STORE_DATA =>
				if(IsFooter = '1') then
					if(DinSel = "11") then
						NextState <= READ_COMPLETE;
					else
						NextState <= INC_DIN_SEL;
					end if;
				else
					if(FULL = '1') then
						NextState <= WAIT_FULL;
					else
						NextState <= WRITE_DATA;
					end if;
				end if;
			when WAIT_FULL =>
				if(FULL = '1') then
					NextState <= CurrentState;
				else
					NextState <= WRITE_DATA;
				end if;
			when WRITE_DATA =>
				NextState <= READ_DATA;
			when INC_DIN_SEL =>
				NextState <= CLEAR_RADDR;
			when CLEAR_RADDR =>
				NextState <= READ_DATA;
			when READ_COMPLETE =>
				NextState <= IDLE;
		end case;
	end process;

	DoutEnable <= '1' when(CurrentState = STORE_DATA) else '0';

	RaddrCountClear <= '1' when(CurrentState = IDLE or
	                            CurrentState = CLEAR_RADDR) else
							 '0';
	RaddrCountUp <= '1' when(CurrentState = WRITE_DATA) else '0';

	DinSelCountClear <= '1' when(CurrentState = IDLE) else '0';
	DinSelCountUp <= '1' when(CurrentState = INC_DIN_SEL) else '0';

	WE <= '1' when(CurrentState = WRITE_DATA) else '0';
	Rcomp <= '1' when(CurrentState = READ_COMPLETE) else '0';
	BUSY <= '1' when(CurrentState /= IDLE) else '0';
end RTL;
