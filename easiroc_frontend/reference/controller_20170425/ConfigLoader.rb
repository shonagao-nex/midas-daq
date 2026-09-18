#!/bin/env ruby

require 'yaml'
require 'digest'

class ConfigLoader
  def initialize(registerAttribute, registerValueAlias, defaultRegisterValue,
                 registerValue, inputDAC, pedestalSuppressionValue,calibration)
    check_hash(registerAttribute, registerValueAlias, defaultRegisterValue)
    @registerAttribute    = readYAML(registerAttribute)
    @defaultRegisterValue = readYAML(defaultRegisterValue)
    @registerValue        = readYAML(registerValue)
    @registerValueAlias   = readYAML(registerValueAlias)
    inputDac              = readYAML(inputDAC)
    @pedestalSuppression  = readYAML(pedestalSuppressionValue)
    @calibration          = readYAML(calibration)
    
    resolveSame
    resolveAlias
    
    @easiroc1SlowControl = @defaultRegisterValue.clone
    overwriteRegisterValue(@easiroc1SlowControl, @registerValue['EASIROC1'])
    overwriteRegisterValue(@easiroc1SlowControl, inputDac['EASIROC1']);
    
    @easiroc2SlowControl = @defaultRegisterValue.clone
    overwriteRegisterValue(@easiroc2SlowControl, @registerValue['EASIROC2'])
    overwriteRegisterValue(@easiroc2SlowControl, inputDac['EASIROC2']);
    
    @probeSlowControl1 = [@registerValue['Probe 1'], @registerValue['Probe Channel 1']]
    @probeSlowControl2 = [@registerValue['Probe 2'], @registerValue['Probe Channel 2']]
    
    @readRegister1 = @registerValue['High Gain Channel 1']
    @readRegister2 = @registerValue['High Gain Channel 2']
    
    @usrClkOutValue  = @registerValue['UsrClkOut']
    @selectableLogic = @registerValue['SelectableLogic']
    @triggerValues   = @registerValue['Trigger']
    
    validate
  end
  
  def toEasiroc1SlowControl
    toEasirocSlowControl(@easiroc1SlowControl)
  end
  
  def toEasiroc2SlowControl
    toEasirocSlowControl(@easiroc2SlowControl)
  end
  
  def toProbe1SlowControl
    if @probeSlowControl1[1] == -1
      return Array.new.fill(0, 0...20)
    end
    
    if @probeSlowControl1[1] > 31
      return Array.new.fill(0, 0...20)
    end
    
    toProbeSlowControl(@probeSlowControl1)
  end
  
  def toProbe2SlowControl
    if @probeSlowControl2[1] == -1
      return Array.new.fill(0, 0...20)
    end
    
    if @probeSlowControl2[1] < 32
      return Array.new.fill(0, 0...20)
    end
    
    toProbeSlowControl([@probeSlowControl2[0], @probeSlowControl2[1] - 32])
  end
  
  def toReadRegister1
    if @readRegister1 == -1
      return -1
    end
    
    if @readRegister1 > 31
      return -1
    else
      return @readRegister1
    end
  end
  
  def toReadRegister2
    if @readRegister2 == -1
      return -1
    end
    
    if @readRegister2 < 32
      return -1
    else
      return @readRegister2 - 32
    end
  end
  
  def toPedestalSuppression
    @pedestalSuppression
  end
  
  def toSelectableLogic
    selectableLogic88bits = Array.new.fill(0,0...11)
    
    pattern = @selectableLogic['Pattern'].split("_").map{|i| i}
    case pattern[0]
    when 'OneCh'
      trigger_channel = pattern[1].to_i
      unless 0<=trigger_channel && trigger_channel<=63
        puts "Error: SelectableLogic, 'Pattern: 'OneCh_#' must be 0..63"
        raise ''
      end	
      selectableLogic88bits[0] = 0
      selectableLogic88bits[1] = trigger_channel
    when 'Or32u'
      selectableLogic88bits[0] = 1
    when 'Or32d'
      selectableLogic88bits[0] = 2
    when 'Or64'
      selectableLogic88bits[0] = 3
    when 'Or32And'
      selectableLogic88bits[0] = 4
    when 'Or16And' 
      selectableLogic88bits[0] = 5
    when 'And32u'
      selectableLogic88bits[0] = 6
    when 'And32d'
      selectableLogic88bits[0] = 7
    when 'And64'
      selectableLogic88bits[0] = 8
    when 'And32Or'
      selectableLogic88bits[0] = 9
    end
    
    hitNumTh = @selectableLogic['HitNum Threshold']
    selectableLogic88bits[2] = hitNumTh
    
    channels = @selectableLogic['And Channels']
    andLogicChannels = Array.new()
    if channels.is_a?(Fixnum)
      andLogicChannels = [channels]
    else
      andLogicChannels = channels.split(" ").map{|i| i.to_i}
    end
    andLogicChannels.each do |i|
      if(-1<i && i<64 )
        selectableLogic88bits[10-i/8] |= (1 << i%8)
      end
    end
    
    selectableLogic88bits
    
  end

  def toTriggerWidth
    intTrigWidth = 
      case @triggerValues['Width']
      when 'raw'
        0
      else
        (@triggerValues['Width'].split('ns')[0].to_i-38)/8
      end
    intTrigWidth
  end
 
  def toTriggerMode
    intTrigMode = @triggerValues['Mode']
  end

  def toTriggerDelay
    delay1 = @triggerValues['DelayTrigger'].to_i
    delay2 = @triggerValues['DelayHold'].to_i
    delay3 = @triggerValues['DelayL1Trig'].to_i
    delay = [ delay1==-1 ? 18 : delay1,
	      delay2==-1 ? 8  : delay2,
	      delay3==-1 ? 13 : delay3]
  end

  def toHVControl
    coeff = @calibration['HVControl']
  end

  def toMadcCalibration
    coeff = @calibration['MonitorADC']
  end

  def timeWindow
    @registerValue['TimeWindow'].split('ns')[0].to_i
  end
  
  def toUsrClkOutRegister
    regData =
      case @usrClkOutValue
      when 'OFF'
        0
      when 'ON'
        1
      when '1Hz'
        2
      when '10Hz'
        3
      when '100Hz'
        4
      when '1kHz'
        5
      when '10kHz'
        6
      when '100kHz'
        7
      when '3MHz'
        8
      end
    regData
  end

  def summary
    registerNames = [
      "Time Constant HG Shaper",
      "Time Constant LG Shaper",
      "Capacitor HG PA Fdbck",
      "Capacitor LG PA Fdbck"
    ]
    
    easirocs = [
      ["EASIROC1", @easiroc1SlowControl],
      ["EASIROC2", @easiroc2SlowControl]
    ]
    
    ret = ''
    
    easirocs.each do |easiroc|
      key = easiroc[0]
      values = easiroc[1]
      
      ret << "#{key}\n"
      registerNames.each do |reg|
        value = values[reg]
        aliasName = @registerValueAlias.select {|k, v|
          Regexp.new(k) =~ reg
        }.values[0].invert[value]
        ret << "    #{reg}: #{aliasName}\n"
      end
    end
    ret
  end
  
  private
  def readYAML(fileName)
    YAML.load_file(fileName)
  end
  
  def overwriteRegisterValue(base, new)
    base.each_key do|key|
      if new.has_key?(key)
        base[key] = new[key]
      end
    end
  end
  
  def fillBit(array, value, bitsOfValue, position)
    bitNumOfRegister = 8
    indexOfArray = position / bitNumOfRegister
    bitPosition = position % bitNumOfRegister
    
    if bitPosition + bitsOfValue > bitNumOfRegister
      numOfBitsToStoreAtThisTime = bitNumOfRegister - bitPosition
      
      valueToStoreBefore = value >> numOfBitsToStoreAtThisTime
      bitsOfValueToStoreBefore = bitsOfValue - numOfBitsToStoreAtThisTime
      positionToStoreBefore = position + numOfBitsToStoreAtThisTime
      fillBit(array, valueToStoreBefore, bitsOfValueToStoreBefore,
              positionToStoreBefore)
      
      bitMask = 1
      (numOfBitsToStoreAtThisTime - 1).times do
        bitMask <<= 1
        bitMask |= 1
      end
      
      value &= bitMask
      bitsOfValue = numOfBitsToStoreAtThisTime
    end
    
    bitMask = 1
    (bitsOfValue - 1).times do
      bitMask <<= 1
      bitMask |= 1
    end
    bitMaskShift = bitMask << bitPosition
    
    array[indexOfArray] &= ~bitMaskShift
    array[indexOfArray] |= (value & bitMask) << bitPosition
  end
  
  def reverseLsbAndMsb(value, bitsOfValue)
    ret = 0
    bitsOfValue.times do |i|
      if value & (1 << i) != 0
        ret |= (1 << (bitsOfValue - i - 1))
      end
    end
    return ret
  end
  
  def toEasirocSlowControl(easirocSlowControl)
    binData = Array.new.fill(0, 0...57)
    bitCounter = 0
    @registerAttribute.each do |register|
      name = register[0]
      attribute = register[1]
      
      value = easirocSlowControl[name]
      if value.is_a?(Fixnum)
        bitCounter = fillRegisterValue(binData, value, attribute, bitCounter)
      elsif value.is_a?(Array)
        value.each do |i|
          bitCounter = fillRegisterValue(binData, i, attribute, bitCounter)
        end
      end
    end
    
    binData.map!{|i| reverseLsbAndMsb(i, 8)}
    return binData.reverse!
  end
   
  def toProbeSlowControl(probeSlowControl)
    binData = Array.new.fill(0, 0...20)
    bitToSet =
      case probeSlowControl[0]
      when 'Out_PA_HG'
        probeSlowControl[1] * 2
      when 'Out_PA_LG'
        probeSlowControl[1] * 2 + 1
      when 'Out_ssh_HG'
        64 + probeSlowControl[1] * 2
      when 'Out_ssh_LG'
        64 + probeSlowControl[1] * 2 + 1
      when 'Out_fs'
        128 + probeSlowControl[1]
      end
    
    fillBit(binData, 1, 1, bitToSet)
    
    binData.map!{|i| reverseLsbAndMsb(i, 8)}
    return binData.reverse!
  end

  def fillRegisterValue(binData, value, attribute, bitCounter)
    if attribute['BitOrder'] == 'MSBtoLSB'
      value = reverseLsbAndMsb(value, attribute['Bits'])
    end
    if attribute['ActiveLow']
      value ^= ([1] * attribute['Bits']).inject{|a, b| (a << 1) | b}
    end
    fillBit(binData, value, attribute['Bits'], bitCounter)
    return bitCounter + attribute['Bits']
  end
  
  def resolveSame
    @registerValue['EASIROC2'].each do |name, value|
      if value == 'same'
        @registerValue['EASIROC2'][name] = @registerValue['EASIROC1'][name]
      end
    end
  end
  
  def resolveAlias
    resolveAliasSub(@registerValue['EASIROC1'])
    resolveAliasSub(@registerValue['EASIROC2'])
    resolveAliasSub(@defaultRegisterValue)
  end
  
  def resolveAliasSub(registerValue)
    registerValue.each do |name, value|
      if value.is_a?(String)
        valueAlias = @registerValueAlias.select {|k, v|
          Regexp.new(k) =~ name
        }.values[0]
        
        strippedValue = value.gsub(' ', '')
        unless valueAlias.has_key?(strippedValue)
          puts 'Error: following register value is incorrect'
          puts "\"#{value}\""
          raise ''
        end
        registerValue[name] = valueAlias[strippedValue]
      end
    end
  end
  
  def validate
    validateClass
    validateRegisterName
    validateRegisterValue
    validateProbe
    validateReadRegister
    validatePedestalSuppression
    validateSelectableLogic
    validateTimeWindow
    validateUsrClkOut
    validateTriggerValues
    validateCalibration
  end
  
  def validateClass
    unless @registerValue.is_a?(Hash)
      puts "Error: RegisterValue file structure error"
      raise ""
    end
    
    settingNames = ['EASIROC1','EASIROC2',
                    'Probe 1','Probe Channel 1','Probe 2','Probe Channel 2',
                    'High Gain Channel 1','High Gain Channel 2','SelectableLogic',
		    'TimeWindow','UsrClkOut','Trigger']
    unless (settingNames - @registerValue.keys).empty?
      puts "Error: RegisterValue file structure error"
      raise ""
    end
    
    settingClasses = [Hash, Hash, 
                      String, Fixnum, String, Fixnum, 
                      Fixnum, Fixnum, Hash,
    		      String, String, Hash] 
    unless settingClasses == settingNames[0..11].map{|i| @registerValue[i].class}
      puts "Error: RegisterValue file structure error"
      raise ""
    end

  end
  
  def validateRegisterName
    registerNames = @registerAttribute.map{|i| i[0]}
    registerValueNames1 = @registerValue['EASIROC1'].keys
    registerValueNames2 = @registerValue['EASIROC2'].keys
    unless (registerValueNames1 - registerNames).empty?
      puts 'Error: following register names are incorrect'
      puts registerValueNames1 - registerNames
      raise ""
    end
    unless (registerValueNames2 - registerNames).empty?
      puts 'Error: following register names are incorrect'
      puts registerValueNames2 - registerNames
      raise ""
    end
  end
  
  def validateRegisterValue
    validateRegisterValueSub(@registerValue['EASIROC1'])
    validateRegisterValueSub(@registerValue['EASIROC2'])
  end
  
  def validateRegisterValueSub(registerValue)
    registerValue.each do |name, value|
      attribute = @registerAttribute.find{|i| i[0] == name}[1]
      if attribute.has_key?('Array')
        unless value.is_a?(Array)
          puts "Error: #{name} must be Array"
          raise ""
        end
        
        max = 2 ** attribute['Bits'] - 1
        value.each do |i|
          unless 0 <= i && i <= max
            puts "Error: element of #{name} must be between #{max} and 0"
            raise ""
          end
        end
      else
        unless value.is_a?(Fixnum)
          puts "Error: #{name} must be Fixnum"
          raise ""
        end
        
        max = 2 ** attribute['Bits'] - 1
        unless 0 <= value && value <= max
          puts "Error: #{name} must be between #{max} and 0"
          raise ""
        end
      end
    end
  end
  
  def validateProbe
    probeNames = ['Out_PA_HG', 'Out_PA_LG',
                  'Out_ssh_HG', 'Out_ssh_LG', 'Out_fs']
    unless probeNames.include?(@registerValue['Probe 1'])
      puts "Error: Probe 1 must be Out_PA_HG, OutPA_LG, Out_ssh_HG, Out_ssh_HG, Out_ssh_LG, Out_fs"
      raise ""
    end
    unless probeNames.include?(@registerValue['Probe 2'])
      puts "Error: Probe 2 must be Out_PA_HG, OutPA_LG, Out_ssh_HG, Out_ssh_HG, Out_ssh_LG, Out_fs"
      raise ""
    end
    
    channel1 = @registerValue['Probe Channel 1']
    unless -1 <= channel1 && channel1 <= 31
      puts "Error: Probe Channel 1 must be between 0 and 31, or -1"
      raise ""
    end
    channel2 = @registerValue['Probe Channel 2']
    unless 32 <= channel2 && channel2 <= 63 || channel2 == -1
      puts "Error: Probe Channel 2 must be between 32 and 63, or -1"
      raise ""
    end
  end
  
  def validateReadRegister
    channel1 = @registerValue['High Gain Channel 1']
    unless -1 <= channel1 && channel1 <= 31
      puts "Error: High Gain Channel 1 must be between 0 and 31, or -1"
      raise ""
    end
    channel2 = @registerValue['High Gain Channel 2']
    unless 32 <= channel2 && channel2 <= 63 || channel2 == -1
      puts "Error: High Gain Channel 2 must be between 32 and 63, or -1"
      raise ""
    end
  end
  
  def validatePedestalSuppression
    unless @pedestalSuppression.keys == ['HG', 'LG']
      puts 'Error: PedestalSuppression file syntax error'
      raise ''
    end
    
    unless @pedestalSuppression['HG'].length == 64
      puts 'Error: PedestalSuppression file syntax error'
      raise ''
    end
    
    unless @pedestalSuppression['LG'].length == 64
      puts 'Error: PedestalSuppression file syntax error'
      raise ''
    end
    
    @pedestalSuppression.each_value do |v|
      unless v.all? {|th| 0 <= th && th <= 4095}
        puts 'Error: PedestalSuppression Threshold must be between 4095 and 0'
        raise ''
      end
    end
  end
  
  def validateSelectableLogic
    selectableLogic = @registerValue['SelectableLogic']
    
    settingNames = ['Pattern','HitNum Threshold','And Channels']
    unless (settingNames - selectableLogic.keys).empty?
      puts "Error: SelectableLogic register parameter structure error"
      raise ""
    end
    
    patternName = ['OneCh','Or32u','Or32d','Or64',
                   'Or32And','Or16And',
                   'And32u','And32d','And64','And32Or']
    
    unless patternName.include?(selectableLogic['Pattern'].split('_')[0])
      puts "Error: SelectableLogic, 'Pattern' must be 'OneCh_#','Or32u','Or32d','Or64','Or32And','Or16And','And32u','And32d','And64','And32Or'"
      raise ''
    end
    
    
    if selectableLogic['HitNum Threshold'].class == Fixnum
      unless (0..64) === selectableLogic['HitNum Threshold']
        puts 'Error: SelectableLogic, "HitNum Threshold" must be between 64 and 0'
        raise ''
      end
    else
      puts 'Error: SelectableLogic, "HitNum Threshold" must be between 64 and 0'
      raise ''
    end
    
    andChannels = selectableLogic['And Channels']
    if andChannels.class == Fixnum
      unless -1<=andChannels && andChannels<=63
        puts "Error: SelectableLogic, 'And Channels': must be 0..63 or -1"
        raise ''
      end
    else
      andChannels.split(" ").each do |i|
        if i !~ /^[0-9]+$/
          puts "Error:SelectableLogic 'And Channels': must be 0..63 or -1"
          raise ''
        end
        unless 0<=i.to_i && i.to_i<=63
          puts "Error:SelectableLogic 'And Channels': must be 0..63 or -1"
          raise ''
        end
      end
    end
    
  end
  
  def validateTriggerValues
    mode = @triggerValues['Mode']
    unless mode.class == Fixnum && mode.to_i >= 0 && mode.to_i <= 7
	    puts 'Error: Trigger(Mode) must be 0 - 7.'
	    raise ''
    end

    delay = [@triggerValues['DelayTrigger'].to_i,
             @triggerValues['DelayHold'].to_i,
	     @triggerValues['DelayL1Trig'].to_i]
    delay.map{|i| 
	    unless i>=-1 && i<=253 && i!=0
		    puts 'Error: Trigger(Delay) must be -1 or 1 - 253'
		    raise ''
	    end
    }

    widthValue = @triggerValues['Width'].split('ns')[0]
    if widthValue != 'raw'
      unless widthValue.to_i <= 800 && widthValue.to_i >= 40
        puts 'Error: Trigger(Width) must be "raw" or "40ns - 800ns"'
        raise ''
      end
    end
  end

  def validateTimeWindow
    timeWindow = @registerValue['TimeWindow'].split('ns')[0]
    unless timeWindow =~ /^[0-9]+$/ && (0..4095) === timeWindow.to_i
      puts 'Error: TimeWindow must be between "4095ns" and "0ns"'
      raise ''
    end
  end

  def validateUsrClkOut
    usrClkOutNames = ['OFF','ON','1Hz','10Hz','100Hz',
                      '1kHz','10kHz','100kHz','3MHz'] 
    unless usrClkOutNames.include?(@registerValue['UsrClkOut'])
      puts 'Error: UsrClkOut must be "OFF, ON, 1Hz, 10Hz, 100Hz, 1kHz, 10kHz, 100kHz, 3MHz"'
      raise ''
    end
  end

  def validateCalibration
    
    settingNames = ['HVControl','MonitorADC']
    unless (settingNames - @calibration.keys).empty?
      puts "Error: Calibration parameter structure error. 'HVControl' and 'MonitorADC'"
      raise ""
    end

    settingMadcNames = ['HV','Current','InputDac','Temperature']
    unless (settingMadcNames - @calibration['MonitorADC'].keys).empty?
      puts "Error: Calibration 'MonitorADC' parameter structure error. 'HV, Current, InputDac, Temperature'"
      raise
    end

    @calibration['HVControl'].each do |x|
      unless x.kind_of?(Float)
        puts "Error: Calibration 'HVControl' parameters must be Float."
        raise ''
      end
    end

    @calibration['MonitorADC'].each_value do |x|
      unless x.kind_of?(Float)
        puts "Error: Calibration 'MonitorADC' parameters must be Float."
        raise ''
      end
    end
    
  end

  
  def check_hash(registerAttribute, registerValueAlias, defaultRegisterValue)
    unless Digest::SHA256.file(registerAttribute).hexdigest ==
      '8be1ac0808a4dbead0e0c0520619b0dad8c627aaf0a87cec04fe1d7b49ccde6d'
      puts "Do not rewrite #{registerAttribute}"
      raise ''
    end
    
    unless Digest::SHA256.file(registerValueAlias).hexdigest ==
      'fda353ea2d49ca8a8b8f95a2ce4774d48801ff38cf4c298d97871e50c88823bc'
      puts "Do not rewrite #{registerValueAlias}"
      raise''
    end
    
    unless Digest::SHA256.file(defaultRegisterValue) ==
      '5c57a15477aa2229bd846418c211af905b058a03fc5054a441bd56ed42ab183e'
      puts "Do not rewrite #{defaultRegisterValue}"
      raise ''
    end
    end
end
