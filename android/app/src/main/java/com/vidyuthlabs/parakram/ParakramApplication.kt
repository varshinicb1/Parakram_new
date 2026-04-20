package com.vidyuthlabs.parakram

import android.app.Application
import dagger.hilt.android.HiltAndroidApp

@HiltAndroidApp
class ParakramApplication : Application() {
    override fun onCreate() {
        super.onCreate()
    }
}
