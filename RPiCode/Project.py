import requests
from PyQt5 import QtWidgets as qtw
from PyQt5 import QtGui as qtg
from PyQt5.QtWidgets import QApplication, QMainWindow, QLineEdit, QLabel
import sys

import time

API_KEY = "df8Wk28NPe-a5ZuibHn7uc"
EVENT_NAME = "FromRPi"
DELIMITER = ";"
WINDOW_TITLE = "Enviroment Monitor System"
FILE_NAME = "ReadingData.txt"

TEMP_MIN = 0
TEMP_MAX = 50
PERC_MIN = 0
PERC_MAX = 100

Y_OFFSET = 30

#create a custom window
class MyWindow(QMainWindow):
	def __init__(self, xPos, yPos, xSize, ySize):
		
		#init window
		super(MyWindow, self).__init__()
		self.xSize = xSize
		self.ySize = ySize
		self.setGeometry(xPos, yPos, xSize, ySize)
		self.setWindowTitle(WINDOW_TITLE)
		
		#read file
		self.lastInput = None
		try:
			f = open(FILE_NAME, "r")
			self.lastInput = f.read()
			f.close()
		except: print("No file found")
		
		self.initUI()
		
	#end def
	
	#initializes ui
	def initUI(self):
		
		self.xPos = self.xSize/2
		self.yPos = 10
		self.xAdd = 80
		self.yAdd = (4 * Y_OFFSET)
		self.highSensitivity = False
		
		#render all environment boxes
		self.addEnvironments()
		
		#render sensitivity switches
		self.yPos += self.yAdd
		self.xPos -= self.xAdd
		self.addSensitivitySwitch()
		
		#render error message box
		self.xPos -= self.xAdd
		self.yPos += (self.yAdd / 2)
		self.addErrorText()

		#render submit button
		self.yPos += (self.yAdd / 2)
		self.addSubmitButton()
		
		if (self.lastInput != None): 
			try: self.interpretInput()
			except: 
				print(f'Corrupted Data: {self.lastInput}')
				
		#end if
	#end def
	
	#receives a string from the file and passes it into the fields
	def interpretInput(self):
		lastInput = self.lastInput
		sensivity = bool(int(lastInput[0]))
		
		firstIndex = 2
		lastIndex = len(lastInput) - 1
		if (lastInput[lastIndex] != DELIMITER): lastInput += DELIMIER
		inputFields = lastInput[firstIndex: len(lastInput)]
		
		value = ""
		fieldsArray = []
		
		lastIndex = len(inputFields)
		for charIndex in range(0, lastIndex):
			currentChar = inputFields[charIndex]
			
			if (currentChar != DELIMITER): value += inputFields[charIndex]
			else: 
				fieldsArray.append(value)
				value = ""
			#end if
		#end for

		lastIndex = len(fieldsArray)
		for arrayIndex in range(0, lastIndex):
			currentString = fieldsArray[arrayIndex]
			value = int(currentString)
			
			noEnvs = len(self.fields)
			noFields = len(self.fields[0].states)
			enviroIndex = (int)(arrayIndex / noFields)
			fieldIndex = arrayIndex - (enviroIndex * noFields)
			
			self.fields[enviroIndex].states[fieldIndex].valueEdit.setText(str(value))
		#end for
	#end def
	
	#adds each environment to the gui
	def addEnvironments(self):
		self.fields = []
		tempField = enviroField(self, self.xPos, self.yPos, TEMP_MIN, TEMP_MAX, "Temperature (C)")
		self.fields.append(tempField)
		
		self.yPos += self.yAdd
		humField = enviroField(self, self.xPos, self.yPos, PERC_MIN, PERC_MAX, "Humidity (%)")
		self.fields.append(humField)
		
		self.yPos += self.yAdd
		moistField = enviroField(self, self.xPos, self.yPos, PERC_MIN, PERC_MAX, "Soil Moisture (%)")
		self.fields.append(moistField)
	#end def
	
	#radio buttons that switch sensitivity on/off
	def addSensitivitySwitch(self):
		self.notSensitiveButton = qtw.QRadioButton(self)
		self.notSensitiveButton.setText("Not Sensitive")
		self.notSensitiveButton.move(self.xPos, self.yPos)
		self.notSensitiveButton.clicked.connect(lambda: self.sensitiveSwitch(False))
		
		self.xPos += (2 * self.xAdd)
		self.sensitiveButton = qtw.QRadioButton(self)
		self.sensitiveButton.setText("Sensitive")
		self.sensitiveButton.move(self.xPos, self.yPos)
		self.sensitiveButton.clicked.connect(lambda: self.sensitiveSwitch(True))		
	#end def
	
	#hidden textbox that displays error messages
	def addErrorText(self):
		self.errorMessage = QLabel(self)
		self.errorMessage.move(self.xPos, self.yPos)		
	#end def
	
	#button that uploads fields when pressed
	def addSubmitButton(self):
		self.submitButton = qtw.QPushButton(self)
		self.submitButton.setText("Upload")
		self.submitButton.move(self.xPos, self.yPos)
		self.submitButton.clicked.connect(self.submitButtonClick)		
	#end def
	
	def sensitiveSwitch(self, value):
		self.highSensitivity = value
		print(f'Switch: {self.highSensitivity}, Value: {value}')
	#end def
	
	#verifies fields and then uploads them to webhook URL
	def submitButtonClick(self):
		
		#cycle through all fields for all environments and add them to the value
		VALUE1 = ""
		
		print(self.highSensitivity)
		VALUE1 += str(int(self.highSensitivity)) + DELIMITER #convert bool to integer form, then convert to string and add to value
		
		#catch all fields for all environments
		for enviroField in self.fields:
			
			#catch the values for all fields in environment
			previous = float("nan") #init previous so first value can be anything
			for state in enviroField.states:
				
				#try to convert input to float
				inputString = str(state.valueEdit.text())
				try: value = int(inputString)
				except:
					print("Empty Input")
					self.errorMessage.setText("Empty Fields")
					return
				#end try
				
				#make sure value is larger than previous value
				if (previous >= value):
					print(f'Invalid Value: {inputString}')
					self.errorMessage.setText("Invalid Fields")
					return
				#end if
				
				previous = value
				outputString = str(value)
				self.errorMessage.setText(None)
				VALUE1 += outputString + DELIMITER
			#end for
		#end for
		
		#URL for webhook
		print(VALUE1);
		URL = f'https://maker.ifttt.com/trigger/{EVENT_NAME}/with/key/{API_KEY}'
		VALUES = f'value1={VALUE1}'
		REQUEST = f'{URL}?{VALUES}'

		data = requests.get(REQUEST)
		f = open(FILE_NAME, "w")
		f.write(VALUE1)
		f.close()
		print(data)
	#end def
#end class

#data relating to the environment
class enviroField:
	def __init__(self, win, xPos, yPos, minValue, maxValue, fieldType):
		self.win = win
		self.xPos = xPos
		self.yPos = yPos
		self.minValue = minValue
		self.maxValue = maxValue
		self.fieldType = fieldType
		
		self.render()
	#end def
	
	#renders the environment fields and title
	def render(self):
		xPos = self.xPos
		yPos = self.yPos
		xAdd = 100 			#100 units between each column
		yAdd = Y_OFFSET		#30 units between each row
		
		self.states = []
		
		#adjust label to include min and max values
		self.fieldName = QLabel(self.win)
		fieldText = f'{self.fieldType}: Min: {self.minValue}, Max: {self.maxValue}:'
		self.fieldName.setText(fieldText)
		
		#hack to get the textbox of reasonable size and position
		fieldWidth = len(fieldText) * 10
		self.fieldName.resize(fieldWidth, 20)
		self.fieldName.move(xPos - (fieldWidth / 4), yPos)
		
		self.addFields(xPos, yPos, xAdd, yAdd)
		self.addStates()
	#end def
	
	#adds each level of fields in a pyramid form: ideal value first, low/high safe next, low/high unsafe next, etcetera
	def addFields(self, xPos, yPos, xAdd, yAdd):
		#center point
		yPos += yAdd
		self.ideal = fieldParameter(self.win, xPos, yPos, self.minValue, self.maxValue, "Ideal Value")
		
		#add each field to either side of center
		self.safeLow = fieldParameter(self.win, xPos - xAdd, yPos, self.minValue, self.maxValue, "Safe Low")
		self.safeHigh = fieldParameter(self.win, xPos + xAdd, yPos, self.minValue, self.maxValue, "Safe High")
		
		#add each field to either side of previous
		xAdd += xAdd 			#moving an extra lay out, so double xAdd
		self.unsafeLow = fieldParameter(self.win, xPos - xAdd, yPos, self.minValue, self.maxValue, "Unsafe Low")
		self.unsafeHigh = fieldParameter(self.win, xPos + xAdd, yPos, self.minValue, self.maxValue, "Unsafe High")		
	#end def
	
	#add the states to array in order of smallest to largest
	def addStates(self):
		self.states.append(self.unsafeLow)
		self.states.append(self.safeLow)
		self.states.append(self.ideal)
		self.states.append(self.safeHigh)
		self.states.append(self.unsafeHigh)		
	#end def
#end class

	#end def
#end class

#data relating to a specific field in an environment
class fieldParameter:
	def __init__(self, win, xPos, yPos, minValue, maxLength, state):
		self.stateName = QLabel(win)
		self.stateName.move(xPos, yPos)
		self.stateName.setText(state)
		
		yPos += Y_OFFSET
		self.valueEdit = QLineEdit(win)
		self.valueEdit.move(xPos, yPos)
        
        #restrict inputs to 0 - 999, no decimals
		validator = qtg.QIntValidator(minValue, maxLength)
		self.valueEdit.setValidator(validator)
	#end def
#end class

def window():
	app = QApplication(sys.argv)
	win = MyWindow(200, 200, 800, 600)
	
	win.show()
	sys.exit(app.exec_())
#end def

window()

#while (True):
#	data = requests.get(REQUEST)
#	print(data)
#	time.sleep(MINUTE)
#end while
