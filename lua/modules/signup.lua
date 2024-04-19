local signup = {}

signup.createModal = function(_, config)
	local loc = require("localize")
	local str = require("str")
	local ui = require("uikit")
	local modal = require("modal")
	local theme = require("uitheme").current
	local ease = require("ease")
	local api = require("system_api", System)
	local conf = require("config")

	local defaultConfig = {
		uikit = ui,
	}

	config = conf:merge(defaultConfig, config)

	ui = config.uikit

	local _year
	local _month
	local _day

	local monthNames = {
		str:upperFirstChar(loc("january")),
		str:upperFirstChar(loc("february")),
		str:upperFirstChar(loc("march")),
		str:upperFirstChar(loc("april")),
		str:upperFirstChar(loc("may")),
		str:upperFirstChar(loc("june")),
		str:upperFirstChar(loc("july")),
		str:upperFirstChar(loc("august")),
		str:upperFirstChar(loc("september")),
		str:upperFirstChar(loc("october")),
		str:upperFirstChar(loc("november")),
		str:upperFirstChar(loc("december")),
	}
	local dayNumbers = {}
	for i = 1, 31 do
		table.insert(dayNumbers, "" .. i)
	end

	local years = {}
	local yearStrings = {}
	local currentYear = math.floor(tonumber(os.date("%Y")))
	local currentMonth = math.floor(tonumber(os.date("%m")))
	local currentDay = math.floor(tonumber(os.date("%d")))

	for i = currentYear, currentYear - 100, -1 do
		table.insert(years, i)
		table.insert(yearStrings, "" .. i)
	end

	local function isLeapYear(year)
		if year % 4 == 0 and (year % 100 ~= 0 or year % 400 == 0) then
			return true
		else
			return false
		end
	end

	local function nbDays(m)
		if m == 2 then
			if isLeapYear(m) then
				return 29
			else
				return 28
			end
		else
			local days = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }
			return days[m]
		end
	end

	local function idealReducedContentSize(content, _, _)
		if content.refresh then
			content:refresh()
		end

		-- print("-- 1 -", content.Width,content.Height)
		-- Timer(1.0, function() content:refresh() print("-- 2 -", content.Width,content.Height) end)
		return Number2(content.Width, content.Height)
	end

	-- initial content, asking for year of birth
	local content = modal:createContent({ uikit = ui })
	content.idealReducedContentSize = idealReducedContentSize

	local node = ui:createFrame(Color(0, 0, 0, 0))
	content.node = node

	content.title = str:upperFirstChar(loc("sign up", "title"))
	content.icon = "🙂"

	local birthdayLabel =
		ui:createText("🎂 " .. str:upperFirstChar(loc("date of birth")), Color(200, 200, 200, 255), "small")
	birthdayLabel:setParent(node)

	local birthdayInfo = ui:createText("", Color(251, 206, 0, 255), "small")
	birthdayInfo:setParent(node)

	local monthInput = ui:createComboBox(str:upperFirstChar(loc("month")), monthNames)
	monthInput:setParent(node)

	local dayInput = ui:createComboBox(str:upperFirstChar(loc("day")), dayNumbers)
	dayInput:setParent(node)

	local yearInput = ui:createComboBox(str:upperFirstChar(loc("year")), yearStrings)
	yearInput:setParent(node)

	local usernameLabel =
		ui:createText("👤 " .. str:upperFirstChar(loc("username")), Color(200, 200, 200, 255), "small")
	usernameLabel:setParent(node)

	local usernameInfo = ui:createText("⚠️ " .. loc("can't be changed"), Color(251, 206, 0, 255), "small")
	usernameInfo:setParent(node)

	local usernameInput = ui:createTextInput("", str:upperFirstChar(loc("don't use your real name!")))
	usernameInput:setParent(node)

	local usernameInfoFrame = nil
	local usernameInfoDT = nil

	local checkDOB = function(config)
		local r = true
		local daysInMonth = nbDays(_month)

		if _year == nil or _month == nil or _day == nil then
			if config and config.errorIfIncomplete == true then
				birthdayInfo.Text = "❌ " .. loc("required")
				birthdayInfo.Color = theme.errorTextColor
				r = false
			else
				birthdayInfo.Text = ""
			end
		elseif _day < 0 or _day > daysInMonth then
			birthdayInfo.Text = "❌ invalid date"
			birthdayInfo.Color = theme.errorTextColor
			r = false
		elseif
			_year > currentYear
			or (_year == currentYear and _month > currentMonth)
			or (_year == currentYear and _month == currentMonth and _day > currentDay)
		then
			birthdayInfo.Text = "❌ users from the future not allowed"
			birthdayInfo.Color = theme.errorTextColor
			r = false
		else
			birthdayInfo.Text = ""
		end

		birthdayInfo.pos.X = node.Width - birthdayInfo.Width
		return r
	end

	monthInput.onSelect = function(self, index)
		System:DebugEvent("SIGNUP_PICK_MONTH")
		_month = index
		self.Text = monthNames[index]
		checkDOB()
	end

	dayInput.onSelect = function(self, index)
		System:DebugEvent("SIGNUP_PICK_DAY")
		_day = index
		self.Text = dayNumbers[index]
		checkDOB()
	end

	yearInput.onSelect = function(self, index)
		System:DebugEvent("SIGNUP_PICK_YEAR")
		_year = years[index]
		self.Text = yearStrings[index]
		checkDOB()
	end

	local checkUsernameTimer = nil
	local checkUsernameRequest = nil
	local checkUsernameKey = nil
	local checkUsernameError = nil
	local reportWrongFormatTimer = nil

	-- callback(ok, key)
	local checkUsername = function(callback, config)
		if checkUsernameTimer ~= nil then
			checkUsernameTimer:Cancel()
			checkUsernameTimer = nil
		end
		if checkUsernameRequest ~= nil then
			checkUsernameRequest:Cancel()
			checkUsernameRequest = nil
		end

		if checkUsernameError ~= nil then
			if callback then
				callback(false, nil)
			end
			return
		end

		if checkUsernameKey ~= nil then
			if callback then
				callback(true, checkUsernameKey)
			end
			return
		end

		local r = true
		local s = usernameInput.Text

		local usernameInfoDTBackup = usernameInfoDT
		usernameInfoDT = nil

		if s == "" then
			if config and config.errorIfEmpty == true then
				usernameInfo.Text = "❌ " .. loc("required")
				usernameInfo.Color = theme.errorTextColor
			else
				usernameInfo.Text = "⚠️ " .. loc("can't be changed")
				usernameInfo.Color = theme.warningTextColor
			end
			r = false
		elseif not s:match("^[a-z].*$") then
			usernameInfo.Text = "❌ " .. loc("must start with a-z")
			usernameInfo.Color = theme.errorTextColor
			r = false
			if reportWrongFormatTimer == nil then
				System:DebugEvent("SIGNUP_WRONG_FORMAT_USERNAME", { username = s })
				reportWrongFormatTimer = Timer(30, function() -- do not report again within the next 30 sec
					reportWrongFormatTimer = nil
				end)
			end
		elseif #s > 15 then
			usernameInfo.Text = "❌ " .. loc("too long")
			usernameInfo.Color = theme.errorTextColor
			r = false
		elseif not s:match("^[a-z][a-z0-9]*$") then
			usernameInfo.Text = "❌ " .. loc("a-z 0-9 only")
			usernameInfo.Color = theme.errorTextColor
			r = false
			if reportWrongFormatTimer == nil then
				print("REPORT WRONG FORMAT")
				System:DebugEvent("SIGNUP_WRONG_FORMAT_USERNAME", { username = s })
				reportWrongFormatTimer = Timer(30, function() -- do not report again within the next 30 sec
					reportWrongFormatTimer = nil
				end)
			end
		else
			local function displayChecking()
				usernameInfoFrame = 0
				usernameInfoDT = usernameInfoDTBackup or 0
				usernameInfo.Text = loc("checking") .. "   "
				usernameInfo.Color = Color(200, 200, 200, 255)
				usernameInfo.pos.X = node.Width - usernameInfo.Width
			end

			local function request()
				checkUsernameRequest = api:checkUsername(s, function(success, res)
					usernameInfoDT = nil
					checkUsernameRequest = nil

					if success == false then
						usernameInfo.Text = "❌ " .. loc("server error")
						usernameInfo.Color = theme.errorTextColor
					elseif res.format ~= true then
						usernameInfo.Text = "❌ format error"
						usernameInfo.Color = theme.errorTextColor
						checkUsernameError = true
					elseif res.appropriate ~= true then
						usernameInfo.Text = "❌ " .. loc("not appropriate")
						usernameInfo.Color = theme.errorTextColor
						checkUsernameError = true
					elseif res.available ~= true then
						usernameInfo.Text = "❌ " .. loc("already taken")
						usernameInfo.Color = theme.errorTextColor
						checkUsernameError = true
					elseif type(res.key) ~= "string" then
						usernameInfo.Text = "❌ " .. loc("server error")
						usernameInfo.Color = theme.errorTextColor
					else
						System:DebugEvent("SIGNUP_ENTERED_VALID_USERNAME")
						usernameInfo.Text = "✅"
						usernameInfo.Color = Color(200, 200, 200, 255)
						checkUsernameKey = res.key
						checkUsernameError = nil
					end

					usernameInfo.pos.X = node.Width - usernameInfo.Width

					if checkUsernameKey ~= nil then
						if callback ~= nil then
							callback(true, checkUsernameKey)
						end
					end
				end)
			end

			if config.noTimer == true then
				displayChecking()
				request()
			else
				checkUsernameTimer = Timer(0.2, function()
					displayChecking()
					-- additional delay for api request
					checkUsernameTimer = Timer(0.3, function()
						usernameInfo.Color = Color(200, 200, 200, 255)
						checkUsernameTimer = nil
						request()
					end)
				end)
				usernameInfo.Text = ""
			end
		end

		usernameInfo.pos.X = node.Width - usernameInfo.Width

		-- if r == true, it means request for server side checks has been scheduled
		if r == false then
			checkUsernameError = true
			if callback ~= nil then
				callback(false, nil)
			end
		end
	end

	local didStartTyping = false
	usernameInput.onTextChange = function(self)
		local backup = self.onTextChange
		self.onTextChange = nil

		local s = str:normalize(self.Text)
		s = str:lower(s)

		self.Text = s
		self.onTextChange = backup

		if didStartTyping == false and self.Text ~= "" then
			didStartTyping = true
			System:DebugEvent("SIGNUP_STARTED_TYPING_USERNAME")
		end

		checkUsernameKey = nil
		checkUsernameError = nil
		checkUsername()
	end

	local signUpButton = ui:createButton(" ✨ " .. str:upperFirstChar(loc("sign up", "button")) .. " ✨ ") -- , { textSize = "big" })
	signUpButton:setParent(node)
	signUpButton:setColor(Color(150, 200, 61), Color(240, 255, 240))

	signUpButton.onRelease = function()
		local dobOK = checkDOB({ errorIfIncomplete = true })

		if dobOK ~= true then
			return
		end

		local usernameCallback = function(ok, key)
			if ok == true and type(key) == "string" then
				local username = usernameInput.Text
				local dob = string.format("%02d-%02d-%04d", _month, _day, _year)

				local modal = content:getModalIfContentIsActive()
				if modal and modal.onSubmit then
					modal.onSubmit(username, key, dob)
				end
			end
		end

		checkUsername(usernameCallback, { errorIfEmpty = true, noTimer = true })
	end

	local tickListener

	content.didBecomeActive = function()
		tickListener = LocalEvent:Listen(LocalEvent.Name.Tick, function(dt)
			if usernameInfoDT then
				usernameInfoDT = usernameInfoDT + dt
				usernameInfoDT = usernameInfoDT % 0.4

				local currentFrame = math.floor(usernameInfoDT / 0.1)

				if currentFrame ~= usernameInfoFrame then
					usernameInfoFrame = currentFrame
					if usernameInfoFrame == 0 then
						usernameInfo.Text = loc("checking") .. "   "
					elseif usernameInfoFrame == 1 then
						usernameInfo.Text = loc("checking") .. ".  "
					elseif usernameInfoFrame == 2 then
						usernameInfo.Text = loc("checking") .. ".. "
					else
						usernameInfo.Text = loc("checking") .. "..."
					end
				end
			end
		end)
	end

	local maxWidth = function()
		return Screen.Width - theme.modalMargin * 2
	end

	local maxHeight = function()
		return Screen.Height - 100
	end

	local terms = ui:createFrame(Color(255, 255, 255, 200))

	content.willResignActive = function()
		if tickListener then
			tickListener:Remove()
			tickListener = nil
		end
		terms:remove()
	end

	local textColor = Color(100, 100, 100)
	local linkColor = Color(4, 161, 255)
	local linkPressedColor = Color(233, 89, 249)

	local termsText = ui:createText(
		loc("By clicking Sign Up, you are agreeing to the Terms of Use and aknowledging the Privacy Policy."),
		textColor,
		"small"
	)
	termsText:setParent(terms)

	local termsBtn = ui:createButton(
		"Terms",
		{ textSize = "small", borders = false, shadow = false, underline = true, padding = false }
	)
	termsBtn:setColor(Color(0, 0, 0, 0), linkColor)
	termsBtn:setColorPressed(Color(0, 0, 0, 0), linkPressedColor)
	termsBtn:setParent(terms)
	termsBtn.onRelease = function()
		System:OpenWebModal("https://cu.bzh/terms")
	end

	local separator = ui:createText("-", textColor, "small")
	separator:setParent(terms)

	local privacyBtn = ui:createButton(
		"Privacy",
		{ textSize = "small", borders = false, shadow = false, underline = true, padding = false }
	)
	privacyBtn:setColor(Color(0, 0, 0, 0), linkColor)
	privacyBtn:setColorPressed(Color(0, 0, 0, 0), linkPressedColor)
	privacyBtn:setParent(terms)
	privacyBtn.onRelease = function()
		System:OpenWebModal("https://cu.bzh/privacy")
	end

	local position = function(modal, forceBounce)
		termsText.object.MaxWidth = modal.Width - theme.paddingTiny * 2

		local termsHeight = termsText.Height + termsBtn.Height + theme.paddingTiny * 3

		local p = Number3(
			Screen.Width * 0.5 - modal.Width * 0.5,
			Screen.Height * 0.5 - modal.Height * 0.5 + (termsHeight + theme.padding) * 0.5,
			0
		)

		if not modal.updatedPosition or forceBounce then
			modal.LocalPosition = p - { 0, 100, 0 }
			modal.updatedPosition = true
			ease:outElastic(modal, 0.3).LocalPosition = p
		else
			modal.LocalPosition = p
		end

		terms.Width = modal.Width
		terms.Height = termsHeight

		terms.pos.X = p.X + modal.Width * 0.5 - terms.Width * 0.5
		terms.pos.Y = p.Y + -terms.Height - theme.padding

		termsText.pos.X = terms.Width * 0.5 - termsText.Width * 0.5
		termsText.pos.Y = terms.Height - termsText.Height - theme.paddingTiny

		local w = termsBtn.Width + separator.Width + privacyBtn.Width + theme.padding * 2

		termsBtn.pos.Y = theme.paddingTiny
		termsBtn.pos.X = terms.Width * 0.5 - w * 0.5

		separator.pos.Y = theme.paddingTiny
		separator.pos.X = termsBtn.pos.X + termsBtn.Width + theme.padding

		privacyBtn.pos.Y = theme.paddingTiny
		privacyBtn.pos.X = separator.pos.X + separator.Width + theme.padding
	end

	local popup = modal:create(content, maxWidth, maxHeight, position, ui)
	popup.terms = terms

	popup.onSuccess = function() end

	popup.bounce = function(_)
		position(popup, true)
	end

	node.refresh = function(self)
		-- signUpButton.Width = nil
		-- signUpButton.Width = signUpButton.Width * 1.5

		self.Width = math.min(400, Screen.Width - Screen.SafeArea.Right - Screen.SafeArea.Left - theme.paddingBig * 2)
		self.Height = birthdayLabel.Height
			+ theme.paddingTiny
			+ monthInput.Height
			+ theme.padding
			+ usernameLabel.Height
			+ theme.paddingTiny
			+ usernameInput.Height
			+ theme.paddingBig
			+ signUpButton.Height

		birthdayLabel.pos.Y = self.Height - birthdayLabel.Height

		birthdayInfo.pos.Y = birthdayLabel.pos.Y
		birthdayInfo.pos.X = self.Width - birthdayInfo.Width

		local thirdWidth = self.Width / 3.0

		monthInput.Width = thirdWidth
		monthInput.pos.Y = birthdayLabel.pos.Y - theme.paddingTiny - monthInput.Height

		dayInput.Width = thirdWidth
		dayInput.pos.X = monthInput.pos.X + monthInput.Width
		dayInput.pos.Y = monthInput.pos.Y

		yearInput.Width = thirdWidth
		yearInput.pos.X = dayInput.pos.X + dayInput.Width
		yearInput.pos.Y = dayInput.pos.Y

		usernameLabel.pos.Y = monthInput.pos.Y - theme.padding - usernameLabel.Height

		usernameInfo.pos.Y = usernameLabel.pos.Y
		usernameInfo.pos.X = self.Width - usernameInfo.Width

		usernameInput.Width = self.Width
		usernameInput.pos.Y = usernameLabel.pos.Y - theme.paddingTiny - usernameInput.Height

		signUpButton.pos.X = self.Width * 0.5 - signUpButton.Width * 0.5
	end

	return popup
end

return signup
