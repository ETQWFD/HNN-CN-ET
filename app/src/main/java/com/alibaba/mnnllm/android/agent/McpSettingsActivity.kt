// HNN CN ET v1.0.3: MCP connection management screen.
package com.alibaba.mnnllm.android.agent

import android.app.Activity
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import com.alibaba.mnnllm.android.R
import com.alibaba.mnnllm.android.databinding.ActivityMcpSettingsBinding

class McpSettingsActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMcpSettingsBinding
    private val servers = mutableListOf<McpServer>()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMcpSettingsBinding.inflate(layoutInflater)
        setContentView(binding.root)
        binding.toolbar.setNavigationOnClickListener { finish() }
        servers.clear()
        servers.addAll(McpConfigStore.loadServers(this))
        renderList()
        binding.btnAdd.setOnClickListener { showAddDialog() }
    }

    private fun renderList() {
        binding.serverList.removeAllViews()
        if (servers.isEmpty()) {
            val empty = TextView(this).apply {
                text = getString(R.string.mcp_empty)
                setPadding(16, 24, 16, 24)
            }
            binding.serverList.addView(empty)
            return
        }
        servers.forEachIndexed { index, server ->
            val row = LayoutInflater.from(this)
                .inflate(R.layout.item_mcp_server, binding.serverList, false)
            row.findViewById<TextView>(R.id.tv_name).text = server.name
            row.findViewById<TextView>(R.id.tv_url).text = server.url
            row.findViewById<Button>(R.id.btn_test).setOnClickListener {
                testServer(server, row.findViewById(R.id.tv_status))
            }
            row.findViewById<Button>(R.id.btn_delete).setOnClickListener {
                servers.removeAt(index)
                McpConfigStore.saveServers(this, servers)
                renderList()
            }
            binding.serverList.addView(row)
        }
    }

    private fun testServer(server: McpServer, statusView: TextView?) {
        statusView?.visibility = View.VISIBLE
        statusView?.text = getString(R.string.mcp_testing)
        statusView?.setTextColor(0xFF9E9E9E.toInt())
        Thread {
            val (tools, error) = McpClient.listToolsWithError(server)
            runOnUiThread {
                if (error != null) {
                    val reason = when (error) {
                        "timeout" -> getString(R.string.mcp_err_timeout)
                        "connect" -> getString(R.string.mcp_err_connect)
                        else -> getString(R.string.mcp_test_fail, error)
                    }
                    statusView?.text = reason
                    statusView?.setTextColor(0xFFE53935.toInt())
                } else if (tools.isEmpty()) {
                    statusView?.text = getString(R.string.mcp_err_no_tools)
                    statusView?.setTextColor(0xFFFB8C00.toInt())
                } else {
                    statusView?.text = getString(R.string.mcp_test_ok, tools.size)
                    statusView?.setTextColor(0xFF00B38A.toInt())
                }
            }
        }.start()
    }

    private fun showAddDialog() {
        val view = LayoutInflater.from(this).inflate(R.layout.dialog_mcp_add, null)
        val nameInput = view.findViewById<EditText>(R.id.input_name)
        val urlInput = view.findViewById<EditText>(R.id.input_url)
        AlertDialog.Builder(this)
            .setTitle(R.string.mcp_add)
            .setView(view)
            .setPositiveButton(R.string.mcp_add) { _, _ ->
                val name = nameInput.text.toString().trim()
                var url = urlInput.text.toString().trim()
                if (name.isBlank() || url.isBlank()) {
                    Toast.makeText(this, "名称和地址不能为空", Toast.LENGTH_SHORT).show()
                    return@setPositiveButton
                }
                if (!url.startsWith("http://") && !url.startsWith("https://")) {
                    url = "http://$url"
                }
                servers.add(McpServer(name, url))
                McpConfigStore.saveServers(this, servers)
                renderList()
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }
}
